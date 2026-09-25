#include "apprecenttasks.h"
#include "appsession.h"
#include "recentsnapshot.h"
#include <QRegularExpression>
#include <QSet>

namespace {
const char snapshotCommand[] =
    "u=$(am get-current-user) || exit 1; printf 'QSC_USER:%s\\n' \"$u\"; "
    "dumpsys activity recents || exit 1; "
    "u=$(am get-current-user) || exit 1; printf '\\nQSC_USER_END:%s\\n' \"$u\"";

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
    m_verifyingClose = false; m_fresh = false; ++m_contextRevision;
    if (!poll.isEmpty()) m_commands->cancel(poll);
    if (!close.isEmpty()) m_commands->cancel(close);
}
void AppRecentTasks::refresh() {
    if (!m_running || !m_pollTag.isEmpty() || !m_closeTag.isEmpty()) return;
    m_pollTag = nextTag("snapshot");
    m_commands->run(m_pollTag, m_serial, {"shell", QString::fromLatin1(snapshotCommand)}, 6000);
}
void AppRecentTasks::setStatus(const QString &message) {
    if (m_status == message) return;
    m_status = message; emit statusChanged();
}
bool AppRecentTasks::parse(const QString &output, QStringList *packages, int *user) {
    return RecentSnapshot::parse(output, packages, user, nullptr);
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
    m_commands->run(m_closeTag, m_serial, {"shell", command}, 6000);
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
        if (!success || RecentSnapshot::commandError(output + error) || output.trimmed() != "QSC_CLOSE_SENT") {
            finishClose(false, tr("关闭失败；未隐藏标签。%1").arg((output + ' ' + error).trimmed().left(240))); refresh(); return;
        }
        m_verifyingClose = true; refresh(); return;
    }
    if (m_pollTag.isEmpty() || tag != m_pollTag) return; // A cancelled/old response cannot change a new session.
    m_pollTag.clear();
    QStringList packages; int user = -1; QString reason;
    const bool parsed = success && !RecentSnapshot::commandError(error)
        && RecentSnapshot::parse(output, &packages, &user, &reason);
    if (!parsed && reason.isEmpty()) reason = error.trimmed().isEmpty() ? QStringLiteral("adb-command-failed") : error.trimmed().left(240);
    // Local, bounded diagnostics only. Never collect Intent extras or scripts.
    m_diagnostics = QString("QtScrcpy 0.4.4-rc.1\nquery=dumpsys activity recents\nadbSuccess=%1\ncharacters=%2\nresult=%3\n")
        .arg(success).arg(output.size()).arg(reason);
    const QRegularExpression structural("(?:TaskRecord|Task)\\{|RecentTaskInfo|(?:mUserId|userId|U)[ \\t]*=|(?:mActivityComponent|realActivity)[ \\t]*=");
    QHash<QString, int> counts; auto matches = structural.globalMatch(output);
    while (matches.hasNext()) ++counts[matches.next().captured(0)];
    auto keys = counts.keys(); keys.sort();
    for (const auto &key : keys) m_diagnostics += QString("field[%1]=%2\n").arg(key).arg(counts.value(key));
    if (!parsed) {
        m_fresh = false;
        const auto message = tr("最近任务同步失败（保留列表，关闭停用）：%1；右键状态栏可复制诊断").arg(reason.left(180));
        if (m_verifyingClose) finishClose(false, tr("关闭命令已发送，但无法核实最近任务；没有假定应用已移除。"));
        else { setStatus(message); emit changed(); }
        return;
    }
    const bool switchedUser = m_user >= 0 && user != m_user;
    const bool changedList = !m_hasSnapshot || packages != m_packages || switchedUser || !m_fresh;
    if (m_user != user) ++m_contextRevision;
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
