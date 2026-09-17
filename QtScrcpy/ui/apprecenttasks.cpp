#include "apprecenttasks.h"
#include "appsession.h"
#include <QRegularExpression>
#include <QSet>

namespace {
const char snapshotCommand[] =
    "u=$(am get-current-user) || exit 1; printf 'QSC_USER:%s\\n' \"$u\"; "
    "dumpsys activity recents || exit 1; "
    "u=$(am get-current-user) || exit 1; printf '\\nQSC_USER_END:%s\\n' \"$u\"";
bool commandError(const QString &text) {
    return text.contains("Exception") || text.contains("Permission Denial", Qt::CaseInsensitive)
        || text.contains("Error:", Qt::CaseInsensitive) || text.contains("permission denied", Qt::CaseInsensitive);
}
}
AppRecentTasks::AppRecentTasks(const QString &serial, AppCommands *commands, QObject *parent)
    : QObject(parent), m_commands(commands), m_serial(serial) {
    m_status = tr("最近任务尚未同步（暂显示历史标签）");
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &AppRecentTasks::refresh);
    connect(commands, &AppCommands::finished, this, &AppRecentTasks::result);
}
QString AppRecentTasks::nextTag(const QString &kind) {
    return QString("tasks-%1-%2").arg(kind).arg(++m_generation);
}
void AppRecentTasks::start() { if (!m_running) { m_running = true; m_timer.start(); refresh(); } }
void AppRecentTasks::stop() {
    m_running = false; m_timer.stop();
    const auto poll = m_pollTag, close = m_closeTag;
    m_pollTag.clear(); m_closeTag.clear(); m_closingPackage.clear();
    m_verifyingClose = false; m_fresh = false;
    if (!poll.isEmpty()) m_commands->cancel(poll);
    if (!close.isEmpty()) m_commands->cancel(close);
}
void AppRecentTasks::refresh() {
    if (!m_running || !m_pollTag.isEmpty() || !m_closeTag.isEmpty()) return;
    m_pollTag = nextTag("snapshot");
    m_commands->run(m_pollTag, m_serial, {"shell", QString::fromLatin1(snapshotCommand)}, 4000);
}
void AppRecentTasks::setStatus(const QString &message) {
    if (m_status == message) return;
    m_status = message; emit statusChanged();
}
bool AppRecentTasks::parse(const QString &output, QStringList *packages, int *user) {
    if (!packages || !user || output.size() > 4 * 1024 * 1024) return false;
    const QString text = QString(output).replace('\r', "");
    if (commandError(text)) return false;
    const auto begin = QRegularExpression("\\AQSC_USER:([0-9]+)\\n").match(text);
    const auto end = QRegularExpression("\\nQSC_USER_END:([0-9]+)\\s*\\z").match(text);
    if (!begin.hasMatch() || !end.hasMatch() || begin.captured(1) != end.captured(1)) return false;
    bool userOk = false; const int activeUser = begin.captured(1).toInt(&userOk);
    if (!userOk || activeUser < 0) return false;
    QString body = text.mid(begin.capturedEnd(), end.capturedStart() - begin.capturedEnd());
    // AOSP appends a second, independently formatted RecentTaskInfo section.
    // Do not let its fields become part of the last Task/TaskRecord block.
    const auto visibleSection = QRegularExpression("(?:^|\\n)[ \\t]*Visible recent tasks[^\\n]*:").match(body);
    if (visibleSection.hasMatch()) body.truncate(visibleSection.capturedStart());
    if (!body.contains("ACTIVITY MANAGER RECENT TASKS")) return false;
    const QRegularExpression header("(?:^|\\n)[ \\t]*\\*?[ \\t]*Recent #([0-9]+):[ \\t]*(?:TaskRecord|Task)\\{");
    QList<int> starts; auto matches = header.globalMatch(body);
    while (matches.hasNext()) { starts.append(matches.next().capturedStart()); if (starts.size() > 512) return false; }
    if (starts.isEmpty() && (body.contains("Recent #") || body.contains("Task{" ) || body.contains("TaskRecord{"))) return false;
    QStringList found; QSet<QString> seen;
    const QRegularExpression userId("\\buserId=([0-9]+)\\b"), shortUser("\\bU=([0-9]+)\\b");
    const QRegularExpression component("\\b(?:realActivity|mRealActivity|mActivityComponent)=([A-Za-z][A-Za-z0-9_.]*)/[A-Za-z0-9_.$]+");
    const QRegularExpression systemTask("\\b(?:type|activityType)=(?:home|recents|dream|assistant)\\b");
    const QRegularExpression numericType("\\bactivityType=([0-9]+)\\b");
    for (int i = 0; i < starts.size(); ++i) {
        const int last = i + 1 < starts.size() ? starts[i + 1] : body.size();
        const QString block = body.mid(starts[i], last - starts[i]);
        auto uid = userId.match(block); if (!uid.hasMatch()) uid = shortUser.match(block);
        if (!uid.hasMatch()) return false; // Do not guess Android users on vendor dumps.
        bool ok = false; const int taskUser = uid.captured(1).toInt(&ok);
        if (!ok) return false;
        if (taskUser != activeUser || systemTask.match(block).hasMatch() || block.contains("isAvailable=false")) continue;
        const auto activityType = numericType.match(block);
        // AOSP numeric types: undefined=0, standard=1; never close system task types.
        if (activityType.hasMatch() && activityType.captured(1).toInt() > 1) continue;
        const auto app = component.match(block);
        // A task may be empty after uninstall; only an explicit null is safe to ignore.
        if (!app.hasMatch()) { if (block.contains("realActivity=null")) continue; return false; }
        const QString name = app.captured(1);
        if (!AppBinding::validPackage(name)) return false;
        if (name == "android" || name == "com.android.systemui" || seen.contains(name)) continue;
        found.append(name); seen.insert(name);
    }
    *packages = found; *user = activeUser; return true;
}
bool AppRecentTasks::canClose(const QString &packageName) const {
    return m_running && m_hasSnapshot && m_fresh && m_user >= 0 && !closing()
        && m_lastSuccess.isValid() && m_lastSuccess.elapsed() <= 5000
        && AppBinding::validPackage(packageName) && m_packages.contains(packageName)
        && packageName != "android" && packageName != "com.android.systemui";
}
bool AppRecentTasks::closePackage(const QString &packageName) {
    if (!canClose(packageName) || !m_writeGuard || !m_writeGuard()) return false;
    const QString oldPoll = m_pollTag; m_pollTag.clear();
    if (!oldPoll.isEmpty()) m_commands->cancel(oldPoll);
    m_closingPackage = packageName; m_verifyingClose = false;
    m_closeTag = nextTag("close");
    // Validate the current user on the phone again immediately before the write.
    // Package names are strict allow-listed identifiers; no untrusted shell text.
    const QString command = QString("u=$(am get-current-user) || exit 1; "
        "[ \"$u\" = \"%1\" ] || { echo 'Error: Android user changed'; exit 1; }; "
        "am force-stop --user %1 %2 && printf 'QSC_CLOSE_SENT\\n'").arg(m_user).arg(packageName);
    setStatus(tr("正在关闭应用并核对最近任务…")); emit changed();
    m_commands->run(m_closeTag, m_serial, {"shell", command}, 4000);
    return true;
}
void AppRecentTasks::finishClose(bool success, const QString &message) {
    m_closingPackage.clear(); m_closeTag.clear(); m_verifyingClose = false;
    setStatus(message); emit changed(); emit closeFinished(success, message);
}
void AppRecentTasks::result(const QString &tag, bool success, const QString &output, const QString &error) {
    if (!m_running) return;
    if (!m_closeTag.isEmpty() && tag == m_closeTag) {
        m_closeTag.clear();
        if (!success || commandError(output + error) || output.trimmed() != "QSC_CLOSE_SENT") {
            finishClose(false, tr("关闭失败；未隐藏标签。%1").arg((output + ' ' + error).trimmed().left(240))); refresh(); return;
        }
        m_verifyingClose = true; refresh(); return;
    }
    if (m_pollTag.isEmpty() || tag != m_pollTag) return; // A cancelled/old response cannot change a new session.
    m_pollTag.clear();
    QStringList packages; int user = -1;
    if (!success || commandError(error) || !parse(output, &packages, &user)) {
        m_fresh = false;
        const auto message = tr("最近任务同步失败（保留上次列表，关闭不可用）；%1").arg(error.left(160));
        if (m_verifyingClose) finishClose(false, tr("关闭命令已发送，但无法核实最近任务；没有假定应用已移除。"));
        else { setStatus(message); emit changed(); }
        return;
    }
    const bool switchedUser = m_user >= 0 && user != m_user;
    const bool changedList = !m_hasSnapshot || packages != m_packages || switchedUser || !m_fresh;
    m_user = user; m_packages = packages; m_hasSnapshot = m_fresh = true; m_lastSuccess.start();
    if (switchedUser) emit userChanged();
    if (changedList) emit changed();
    if (m_verifyingClose) {
        if (switchedUser) finishClose(false, tr("Android 用户已切换，无法核实刚才的关闭结果。"));
        else if (packages.contains(m_closingPackage))
            finishClose(true, tr("关闭命令已完成；手机仍保留最近任务卡片，标签按手机状态保留。"));
        else finishClose(true, tr("应用已关闭，最近任务已更新。"));
    } else setStatus(tr("最近任务已同步 · 用户 %1 · 每秒检查").arg(m_user));
}
