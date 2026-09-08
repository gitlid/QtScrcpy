#include "appsession.h"
#include "keymapdocument.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>
#include <QStandardPaths>
#include <algorithm>
#include <memory>
#include "../QtScrcpyCore/include/adbprocess.h"

namespace {
class AdbAppCommands final : public AppCommands {
public:
    using AppCommands::AppCommands;
    ~AdbAppCommands() override { cancelAll(); }
    void run(const QString &tag, const QString &serial, const QStringList &args, int timeoutMs) override {
        if (m_jobs.contains(tag)) return;
        auto *process = new qsc::AdbProcess(this);
        process->setQuiet(true);
        m_jobs.insert(tag, process);
        auto done = std::make_shared<bool>(false);
        auto *timer = new QTimer(process);
        timer->setSingleShot(true);
        auto finish = [this, process, timer, tag, done](bool ok, const QString &error) {
            if (*done) return;
            *done = true;
            timer->stop();
            const QString output = process->getStdOut() + (tag == "names" ? '\n' + process->getErrorOut() : QString());
            m_jobs.remove(tag);
            process->deleteLater();
            emit finished(tag, ok, output, error);
        };
        connect(process, &qsc::AdbProcess::adbProcessResult, process,
                [process, finish](qsc::AdbProcess::ADB_EXEC_RESULT result) {
            if (result == qsc::AdbProcess::AER_SUCCESS_START) return;
            finish(result == qsc::AdbProcess::AER_SUCCESS_EXEC, process->getErrorOut().left(300));
        });
        connect(timer, &QTimer::timeout, process, [process, finish] {
            finish(false, tr("ADB 请求超时"));
            process->kill();
        });
        timer->start(timeoutMs);
        process->execute(serial, args);
    }
    void cancelAll() override {
        const auto jobs = m_jobs;
        m_jobs.clear();
        for (auto *process : jobs) {
            process->disconnect();
            process->kill();
            delete process;
        }
    }
    void cancel(const QString &tag) override {
        auto *process = m_jobs.take(tag);
        if (!process) return;
        process->disconnect(); process->kill(); delete process;
    }
private:
    QHash<QString, qsc::AdbProcess *> m_jobs;
};
}

AppSession::AppSession(qsc::IDevice *device, const QString &serverPath, QObject *parent,
                       AppCommands *commands, const QString &directory)
    : QObject(parent), m_device(device), m_commands(commands ? commands : new AdbAppCommands(this)),
      m_serial(device ? device->getSerial() : QString()), m_serverPath(serverPath),
      m_directory(directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                                         + "/app-profiles" : directory) {
    m_connected = device && !device->isCameraMode() && !device->isFlexDisplay();
    m_timer.setInterval(250);
    connect(&m_timer, &QTimer::timeout, this, &AppSession::tick);
    connect(m_commands, &AppCommands::finished, this, &AppSession::result);
    if (device) {
        connect(device, &qsc::IDevice::deviceDisconnected, this, [this](const QString &) { shutdown(); });
        connect(device, &QObject::destroyed, this, &AppSession::shutdown);
        connect(device, &qsc::IDevice::actionMacroStateChanged, this, [this](bool, bool, int) { deviceStateChanged(); });
        connect(device, &qsc::IDevice::actionMacroApplicationInterrupted, this, [this] {
            if (!locked() || m_manualPaused) return;
            m_autoPaused = true; m_stable.invalidate();
            if (!m_recovery.isValid()) { m_recovery.start(); m_launchAttempts = 0; }
            setStatus(tr("画面方向已改变，暂停并等待目标应用恢复…"));
        });
    }
}
AppSession::~AppSession() { shutdown(); }
void AppSession::start() {
    if (!m_connected || m_timer.isActive()) return;
    setStatus(tr("正在读取手机应用…"));
    m_commands->run("identity", m_serial, {"shell", "getprop", "ro.serialno"}, 3000);
    refreshApps();
    m_timer.start();
    tick();
}
void AppSession::shutdown() {
    if (!m_connected) return;
    m_connected = false;
    m_timer.stop();
    stopMacro();
    m_commands->cancelAll();
    m_probePending = m_launchPending = false;
    setStatus(tr("手机已断开"));
    emit appsChanged();
}
QList<PhoneApp> AppSession::apps() const {
    auto result = m_apps.values();
    std::sort(result.begin(), result.end(), [](const PhoneApp &a, const PhoneApp &b) {
        return a.label.localeAwareCompare(b.label) < 0;
    });
    return result;
}
QString AppSession::label(const QString &packageName) const {
    const auto app = m_apps.value(packageName);
    const auto saved = m_profiles.value(packageName).value("label").toString();
    return (!app.label.isEmpty() ? app.label : !saved.isEmpty() ? saved : packageName).left(160);
}
AppBinding AppSession::binding(const QString &packageName) const {
    AppBinding result;
    if (AppBinding::validPackage(packageName)) { result.packageName = packageName; result.label = label(packageName); }
    return result;
}
QList<PhoneApp> AppSession::parseComponents(const QString &output) {
    QList<PhoneApp> result;
    QSet<QString> seen;
    const QRegularExpression pattern(QStringLiteral("^([A-Za-z][A-Za-z0-9_.]*)/([A-Za-z0-9_.$]+)$"));
    for (const auto &line : output.split('\n')) {
        const auto match = pattern.match(line.trimmed());
        if (!match.hasMatch() || !AppBinding::validPackage(match.captured(1)) || seen.contains(match.captured(1))) continue;
        seen.insert(match.captured(1));
        PhoneApp app; app.packageName = match.captured(1); app.label = app.packageName; app.component = match.captured(0);
        result.append(app);
        if (result.size() == 1024) break;
    }
    return result;
}
QString AppSession::parseForeground(const QString &output) {
    // A focused system overlay must not be mistaken for the activity behind it.
    const QRegularExpression pattern(QStringLiteral("mCurrentFocus=Window\\{[^\\r\\n]*?\\s([A-Za-z][A-Za-z0-9_.]*)/[^\\s}]+"));
    const auto match = pattern.match(output);
    return match.hasMatch() && AppBinding::validPackage(match.captured(1)) ? match.captured(1) : QString();
}
void AppSession::refreshApps() {
    if (!m_connected) return;
    m_commands->run("catalog", m_serial, {"shell", "cmd", "package", "query-activities", "--brief", "--components",
                    "--user", "current", "-a", "android.intent.action.MAIN", "-c", "android.intent.category.LAUNCHER"}, 6000);
}
void AppSession::tick() {
    if (!m_connected || !m_device) return;
    if (locked() && !m_manualPaused) {
        if (m_recovery.isValid() && m_recovery.elapsed() > 15000) {
            failGuard(tr("无法切回 %1，预制操作已停止。请检查手机是否锁屏或应用是否可用。").arg(label(m_target)));
            return;
        }
        if (m_started && !m_device->isActionPaused() && m_lastProbe.isValid() && m_lastProbe.elapsed() > 900) {
            m_internal = true; m_autoPaused = m_device->pauseActionMacro(); m_internal = false;
            if (!m_autoPaused) { failGuard(tr("无法暂停预制操作，已停止。")); return; }
            if (!m_recovery.isValid()) m_recovery.start();
            m_stable.invalidate();
            setStatus(tr("前台检测暂时无响应，已暂停输入…"));
        }
    }
    if (!m_probePending) {
        m_probePending = true;
        m_commands->run("focus", m_serial, {"shell", "dumpsys window | grep mCurrentFocus"}, 2500);
    }
}
void AppSession::result(const QString &tag, bool success, const QString &output, const QString &error) {
    if (!m_connected) return;
    if (tag == "identity") {
        const auto identity = output.trimmed();
        loadProfiles(success && !identity.isEmpty() && identity != "unknown" ? identity : m_serial);
        applyForegroundKeymap();
        emit appsChanged();
    } else if (tag == "catalog") {
        const auto parsed = parseComponents(output);
        if (!success || parsed.isEmpty()) { setStatus(tr("读取应用列表失败，可点击 + 重试。%1").arg(error)); return; }
        QHash<QString, PhoneApp> updated;
        for (auto app : parsed) {
            if (m_apps.contains(app.packageName)) app.label = m_apps.value(app.packageName).label;
            updated.insert(app.packageName, app);
        }
        m_apps = updated;
        ensureTab(m_foreground);
        emit appsChanged();
        // Reuse scrcpy's existing app-name query without cleaning up the live server.
        const QRegularExpression path(QStringLiteral("^/[A-Za-z0-9_./-]+$"));
        if (path.match(m_serverPath).hasMatch())
            m_commands->run("names", m_serial, {"shell", "CLASSPATH=" + m_serverPath, "app_process", "/",
                "com.genymobile.scrcpy.Server", "4.1", "list_apps=true", "cleanup=false", "log_level=info"}, 10000);
    } else if (tag == "names") {
        if (!success) return;
        const QRegularExpression pattern(QStringLiteral("^\\s*[\\*-]\\s+(.+?)\\s+([A-Za-z0-9_]+(?:\\.[A-Za-z0-9_]+)+)\\s*$"), QRegularExpression::MultilineOption);
        auto matches = pattern.globalMatch(output);
        while (matches.hasNext()) {
            const auto match = matches.next();
            if (m_apps.contains(match.captured(2))) m_apps[match.captured(2)].label = match.captured(1).trimmed().left(160);
        }
        emit appsChanged();
    } else if (tag == "focus") {
        m_probePending = false;
        if (success) m_lastProbe.restart();
        observe(success ? parseForeground(output) : QString());
        advanceGuard();
    } else if (tag == "launch") {
        m_launchPending = false;
        if (!success || output.contains("Error:") || output.contains("Exception")) {
            const auto message = tr("应用启动失败：%1").arg((output + " " + error).trimmed().left(240));
            if (locked()) failGuard(message); else { setStatus(message); emit failure(message); }
        }
    }
}
void AppSession::observe(const QString &packageName) {
    if (m_foreground == packageName) return;
    m_foreground = packageName;
    m_stable.invalidate();
    ensureTab(packageName);
    if (!locked()) applyForegroundKeymap();
    emit foregroundChanged(packageName);
    if (!locked()) setStatus(packageName.isEmpty() ? tr("手机桌面、锁屏或系统界面") : tr("当前：%1").arg(label(packageName)));
}
void AppSession::ensureTab(const QString &packageName) {
    if (!m_profilesReady || !m_apps.contains(packageName) || m_tabs.contains(packageName) || m_tabs.size() >= 32) return;
    m_tabs.append(packageName);
    saveProfiles();
    emit appsChanged();
}
void AppSession::activate(const QString &packageName) {
    if (!m_connected || !m_device) return;
    if (locked() && !m_manualPaused) {
        if (packageName != m_target) setStatus(tr("预制操作运行中，将保持 %1 在前台；暂停或停止后可自由切换。").arg(label(m_target)));
        return;
    }
    if (m_device->isActionRecording()) { emit failure(tr("请先结束录制再切换应用。")); return; }
    if (packageName.isEmpty()) {
        if (m_launchPending) return;
        m_launchPending = true;
        m_commands->run("launch", m_serial, {"shell", "am", "start", "-a", "android.intent.action.MAIN", "-c", "android.intent.category.HOME"}, 8000);
    } else { ensureTab(packageName); launch(packageName); }
}
void AppSession::launch(const QString &packageName) {
    if (m_launchPending) return;
    const auto app = m_apps.value(packageName);
    if (!AppBinding::validPackage(packageName) || app.component.isEmpty()) {
        const auto message = tr("应用未安装或没有启动入口：%1").arg(label(packageName));
        if (locked()) failGuard(message); else emit failure(message);
        return;
    }
    m_launchPending = true;
    if (locked()) ++m_launchAttempts;
    setStatus(tr("正在切换到 %1…").arg(label(packageName)));
    m_commands->run("launch", m_serial, {"shell", "am", "start", "--user", "current", "-a", "android.intent.action.MAIN",
        "-c", "android.intent.category.LAUNCHER", "-f", "0x10200000", "-n", "'" + app.component + "'"}, 8000);
}
void AppSession::closeTab(const QString &packageName) {
    if (packageName == m_target) return;
    m_tabs.removeAll(packageName);
    saveProfiles(); emit appsChanged();
}
QString AppSession::keymap(const QString &packageName) const { return m_profiles.value(packageName).value("keymap").toString(); }
bool AppSession::bindKeymap(const QString &packageName, const QString &script) {
    if (!ready() || !AppBinding::validPackage(packageName)) return false;
    KeymapDocument document; QString error;
    if (!script.isEmpty() && (script.toUtf8().size() > 1024 * 1024 || !document.parse(script.toUtf8(), &error))) return false;
    const auto original = m_profiles;
    auto &profile = m_profiles[packageName]; profile["label"] = label(packageName); profile["keymap"] = script;
    if (!saveProfiles()) { m_profiles = original; return false; }
    m_hasAppKeymap = true;
    m_appliedPackage.clear(); m_appliedScript.clear();
    ensureTab(packageName); applyForegroundKeymap(); emit appsChanged();
    return true;
}
QStringList AppSession::macros(const QString &packageName) const {
    QStringList result;
    for (const auto &value : m_profiles.value(packageName).value("macros").toArray()) result.append(value.toString());
    return result;
}
bool AppSession::rememberMacro(const AppBinding &bound, const QString &path) {
    if (bound.isEmpty()) return true;
    if (!ready() || !AppBinding::validPackage(bound.packageName)) return false;
    const auto original = m_profiles;
    auto files = macros(bound.packageName); files.removeAll(path); files.prepend(path);
    while (files.size() > 20) files.removeLast();
    auto &profile = m_profiles[bound.packageName]; profile["label"] = bound.label;
    profile["macros"] = QJsonArray::fromStringList(files);
    if (!saveProfiles()) { m_profiles = original; return false; }
    ensureTab(bound.packageName); emit appsChanged(); return true;
}
void AppSession::applyForegroundKeymap() {
    if (!ready() || !m_device || locked() || m_device->isActionPlaying() || m_device->isActionRecording()) return;
    const auto script = keymap(m_foreground);
    if (!m_hasAppKeymap && script.isEmpty()) return; // Preserve existing global configurations until an app scheme is used.
    if (m_appliedPackage == m_foreground && m_appliedScript == script) return;
    if (m_device->applyAppKeymap(script)) { m_appliedPackage = m_foreground; m_appliedScript = script; }
}
void AppSession::startMacro(const AppBinding &bound, std::function<bool()> play) {
    if (!ready() || !m_device || m_device->isActionPlaying() || m_device->isActionRecording() || preparing()) {
        emit failure(tr("手机或预制操作尚未就绪。")); return;
    }
    if (bound.isEmpty()) { m_device->setActionMacroApplicationBound(false); if (!play()) emit failure(tr("无法开始预制操作。")); return; }
    if (!AppBinding::validPackage(bound.packageName) || !m_apps.contains(bound.packageName)) {
        emit failure(tr("绑定应用未安装或不可启动：%1").arg(bound.label)); return;
    }
    m_target = bound.packageName; m_pendingStart = std::move(play); m_started = false;
    m_device->setActionMacroApplicationBound(true);
    m_manualPaused = m_autoPaused = false; m_launchAttempts = 0;
    m_recovery.start(); m_stable.invalidate(); ensureTab(m_target);
    emit lockChanged();
    launch(m_target); tick();
}
void AppSession::advanceGuard() {
    if (!locked() || m_manualPaused || !m_device) return;
    if (m_foreground != m_target) {
        m_stable.invalidate();
        if (m_started && !m_device->isActionPaused()) {
            m_internal = true; m_autoPaused = m_device->pauseActionMacro(); m_internal = false;
            if (!m_autoPaused) { failGuard(tr("无法暂停预制操作，已停止。")); return; }
        }
        if (!m_recovery.isValid()) { m_recovery.start(); m_launchAttempts = 0; }
        if (!m_launchPending && m_launchAttempts < 3 && m_recovery.elapsed() >= m_launchAttempts * 2000) launch(m_target);
        return;
    }
    if (m_launchPending) return;
    if (!m_stable.isValid()) { m_stable.start(); return; }
    if (m_stable.elapsed() < 500) return;
    if (!m_device->actionMacroScreenMatches()) {
        setStatus(tr("等待 %1 恢复录制时的画面尺寸和方向…").arg(label(m_target)));
        return;
    }
    if (m_pendingStart) {
        const auto play = m_pendingStart; m_pendingStart = nullptr;
        m_internal = true; m_started = true;
        const bool ok = play();
        m_internal = false;
        if (!ok) { failGuard(tr("无法开始预制操作，请检查画面尺寸、方向和起始页面。")); return; }
        emit lockChanged();
    } else if (m_autoPaused) {
        const bool interrupted = m_device->actionMacroInterruptedInput();
        m_internal = true; const bool ok = m_device->resumeActionMacro(); m_internal = false;
        m_autoPaused = false;
        if (!ok) { failGuard(tr("切回后无法继续，预制操作已停止。")); return; }
        if (interrupted) emit failure(tr("已切回目标应用并继续；中断的长按或拖动已释放，跳过该动作组。"));
    }
    m_recovery.invalidate(); m_launchAttempts = 0;
    setStatus(tr("预制操作运行中 · 保持 %1 在前台").arg(label(m_target)));
}
void AppSession::deviceStateChanged() {
    if (m_internal || !m_device) return;
    if (!locked()) { applyForegroundKeymap(); return; }
    if (m_started && !m_device->isActionPlaying()) { stopMacro(); return; }
    if (m_started && m_device->isActionPaused() && !m_autoPaused) {
        m_manualPaused = true; m_recovery.invalidate();
        setStatus(tr("预制操作已暂停 · 可切换应用")); emit lockChanged();
    }
}
void AppSession::pauseMacro() {
    if (!m_device) return;
    if (preparing()) { stopMacro(); return; }
    m_manualPaused = true; m_autoPaused = false; m_recovery.invalidate();
    m_device->pauseActionMacro();
    if (locked()) { setStatus(tr("预制操作已暂停 · 可切换应用")); emit lockChanged(); }
}
void AppSession::resumeMacro() {
    if (!m_device) return;
    if (!locked()) { m_device->resumeActionMacro(); return; }
    m_manualPaused = false; m_autoPaused = true; m_recovery.start(); m_stable.invalidate(); m_launchAttempts = 0;
    launch(m_target); tick(); emit lockChanged();
}
void AppSession::stopMacro() {
    m_commands->cancel("launch"); m_launchPending = false;
    m_target.clear(); m_pendingStart = nullptr; m_started = m_autoPaused = m_manualPaused = false;
    m_recovery.invalidate(); m_stable.invalidate();
    if (m_device) { m_device->setActionMacroApplicationBound(false); m_device->stopActionPlayback(); }
    applyForegroundKeymap(); emit lockChanged();
}
void AppSession::failGuard(const QString &text) { stopMacro(); setStatus(text); emit failure(text); }
void AppSession::setStatus(const QString &text) { if (m_status != text) { m_status = text; emit statusChanged(text); } }

void AppSession::loadProfiles(const QString &identity) {
    const auto hash = QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex();
    m_profilePath = QDir(m_directory).filePath(QString::fromLatin1(hash) + ".json");
    m_profilesReady = true;
    QFile file(m_profilePath);
    if (!file.exists()) { ensureTab(m_foreground); return; }
    bool ok = file.open(QIODevice::ReadOnly) && file.size() <= 16 * 1024 * 1024;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(ok ? file.readAll() : QByteArray(), &error);
    const auto root = document.object();
    ok = ok && error.error == QJsonParseError::NoError && root.value("version").toDouble() == 1
         && root.value("profiles").isObject() && root.value("tabs").isArray() && root.value("profiles").toObject().size() <= 128;
    if (ok) {
        const auto profiles = root.value("profiles").toObject();
        for (auto it = profiles.begin(); it != profiles.end(); ++it) {
            KeymapDocument keymapDocument; QString validation;
            const auto profile = it.value().toObject(); const auto script = profile.value("keymap").toString();
            if (!AppBinding::validPackage(it.key()) || !it.value().isObject() || !profile.value("label").isString() || profile.value("label").toString().size() > 160
                || (profile.contains("keymap") && !profile.value("keymap").isString())
                || (profile.contains("macros") && (!profile.value("macros").isArray() || profile.value("macros").toArray().size() > 20))
                || script.toUtf8().size() > 1024 * 1024 || (!script.isEmpty() && !keymapDocument.parse(script.toUtf8(), &validation))) { ok = false; break; }
            for (const auto &macro : profile.value("macros").toArray()) if (!macro.isString() || macro.toString().size() > 4096) ok = false;
            if (!ok) break;
            m_profiles[it.key()] = profile; m_hasAppKeymap = m_hasAppKeymap || profile.contains("keymap");
        }
        for (const auto &value : root.value("tabs").toArray()) {
            if (!AppBinding::validPackage(value.toString()) || m_tabs.size() >= 32) { ok = false; break; }
            if (!m_tabs.contains(value.toString())) m_tabs.append(value.toString());
        }
    }
    if (!ok) {
        m_profiles.clear(); m_tabs.clear(); m_hasAppKeymap = false; m_readOnlyProfiles = true;
        setStatus(tr("应用配置文件无效，已保留原文件并禁止覆盖：%1").arg(m_profilePath));
        emit failure(m_status);
    }
    ensureTab(m_foreground);
}
bool AppSession::saveProfiles() {
    if (!m_profilesReady || m_readOnlyProfiles || m_profiles.size() > 128 || !QDir().mkpath(m_directory)) return false;
    QJsonObject profiles;
    for (auto it = m_profiles.begin(); it != m_profiles.end(); ++it) profiles[it.key()] = it.value();
    const auto data = QJsonDocument(QJsonObject{{"version", 1}, {"tabs", QJsonArray::fromStringList(m_tabs)}, {"profiles", profiles}}).toJson();
    if (data.size() > 16 * 1024 * 1024) return false;
    QSaveFile file(m_profilePath);
    const bool ok = file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
    if (!ok) emit failure(tr("应用配置保存失败：%1").arg(m_profilePath));
    return ok;
}
