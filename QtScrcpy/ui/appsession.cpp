#include "appsession.h"
#include "keymapdocument.h"
#include "applabels.h"
#include "appcommandprocess.h"
#include "apprecenttasks.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>

AppSession::AppSession(qsc::IDevice *device, const QString &serverPath, QObject *parent,
                       AppCommands *commands, const QString &directory)
    : QObject(parent), m_device(device), m_commands(commands ? commands : new AdbAppCommands(this)),
      m_serial(device ? device->getSerial() : QString()), m_serverPath(serverPath),
      m_directory(directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                                         + "/app-profiles" : directory) {
    m_connected = device && !device->isCameraMode() && !device->isFlexDisplay();
    m_recentTasks = new AppRecentTasks(m_serial, m_commands, this);
    m_recentTasks->setWriteGuard([this] {
        return m_connected && m_device && !locked() && !m_device->isActionPlaying() && !m_device->isActionRecording();
    });
    connect(m_recentTasks, &AppRecentTasks::changed, this, &AppSession::appsChanged);
    connect(m_recentTasks, &AppRecentTasks::statusChanged, this, &AppSession::taskStatusChanged);
    connect(m_recentTasks, &AppRecentTasks::userChanged, this, [this] { stopMacro(); refreshApps(); });
    connect(m_recentTasks, &AppRecentTasks::closeFinished, this, [this](bool ok, const QString &message) {
        setStatus(message); if (!ok) emit failure(message); refreshFocus();
    });
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
    refreshApps(); m_timer.start(); tick(); m_recentTasks->start();
}
void AppSession::shutdown() {
    if (!m_connected) return;
    m_connected = false; m_timer.stop(); m_recentTasks->stop(); stopMacro(); m_commands->cancelAll();
    m_probePending = m_launchPending = m_namesPending = false;
    setStatus(tr("手机已断开")); emit appsChanged();
}
QStringList AppSession::tabs() const {
    if (!m_recentTasks || !m_recentTasks->hasSnapshot()) return m_tabs;
    QStringList visible;
    for (const auto &name : m_recentTasks->packages()) if (m_apps.contains(name)) visible.append(name);
    return visible;
}
QString AppSession::taskStatus() const { return m_recentTasks ? m_recentTasks->status() : QString(); }
bool AppSession::closingApp() const { return m_recentTasks && m_recentTasks->closing(); }
bool AppSession::canCloseApp(const QString &packageName) const {
    return ready() && m_device && m_apps.contains(packageName) && m_recentTasks && m_recentTasks->canClose(packageName);
}
void AppSession::closeApp(const QString &packageName) {
    if (!canCloseApp(packageName)) { emit failure(tr("最近任务尚未确认、应用已退出或正在关闭；请稍后重试。")); return; }
    // The AppBar confirmation covers stopping both bound and unbound macros.
    stopMacro();
    m_device->stopActionRecording(); m_device->prepareKeymapEditing(); m_device->releaseKeyboard();
    if (!m_recentTasks->closePackage(packageName)) emit failure(tr("当前输入或任务状态改变，未关闭应用。"));
}
QList<PhoneApp> AppSession::apps() const {
    auto result = m_apps.values();
    for (auto &app : result) app.label = label(app.packageName);
    std::sort(result.begin(), result.end(), [](const PhoneApp &a, const PhoneApp &b) {
        const int order = a.label.localeAwareCompare(b.label);
        return order != 0 ? order < 0 : a.packageName < b.packageName;
    });
    return result;
}
QString AppSession::label(const QString &packageName) const {
    const auto current = m_apps.value(packageName).label;
    if (AppLabels::usable(current, packageName)) return current;
    const auto cached = m_labels.value(packageName);
    if (AppLabels::usable(cached, packageName)) return cached;
    const auto saved = m_profiles.value(packageName).value("label").toString();
    if (AppLabels::usable(saved, packageName) && saved != tr("名称未读取")) return saved;
    // The package remains available in tooltips/search; never invent a name.
    return tr("名称未读取");
}
AppBinding AppSession::binding(const QString &packageName) const {
    AppBinding result;
    if (AppBinding::validPackage(packageName)) { result.packageName = packageName; result.label = label(packageName); }
    return result;
}
QList<PhoneApp> AppSession::parseComponents(const QString &output) {
    QList<PhoneApp> result; QSet<QString> seen;
    const QRegularExpression pattern(QStringLiteral("^([A-Za-z][A-Za-z0-9_.]*)/([A-Za-z0-9_.$]+)$"));
    for (const auto &line : output.split('\n')) {
        const auto match = pattern.match(line.trimmed());
        if (!match.hasMatch() || !AppBinding::validPackage(match.captured(1)) || seen.contains(match.captured(1))) continue;
        seen.insert(match.captured(1));
        PhoneApp app; app.packageName = match.captured(1); app.component = match.captured(0);
        result.append(app);
        if (result.size() == 1024) break;
    }
    return result;
}
QString AppSession::parseForeground(const QString &output) {
    const QRegularExpression pattern(QStringLiteral("mCurrentFocus=Window\\{[^\\r\\n]*?\\s([A-Za-z][A-Za-z0-9_.]*)/[^\\s}]+"));
    const auto match = pattern.match(output);
    return match.hasMatch() && AppBinding::validPackage(match.captured(1)) ? match.captured(1) : QString();
}
void AppSession::refreshApps() {
    if (!m_connected) return;
    m_commands->run("catalog", m_serial, {"shell", "cmd", "package", "query-activities", "--brief", "--components",
                    "--user", "current", "-a", "android.intent.action.MAIN", "-c", "android.intent.category.LAUNCHER"}, 6000);
}
void AppSession::requestNames() {
    if (!m_connected || m_namesPending) return;
    m_namesPending = true; m_namesStaged = false;
    const QRegularExpression path(QStringLiteral("^/[A-Za-z0-9_./-]+$"));
    if (!path.match(m_serverPath).hasMatch()) { stageNameQuery(); return; }
    m_commands->run("names", m_serial, {"shell", "CLASSPATH=" + m_serverPath, "app_process", "/",
        "com.genymobile.scrcpy.Server", qsc::DeviceParams().serverVersion,
        "list_apps=true", "cleanup=false", "log_level=info"}, 15000);
}
void AppSession::stageNameQuery() {
    // The live server normally unlinks its JAR. Do not overwrite/restart it:
    // use a separate, session-owned copy of the same installed server binary.
    m_namesStaged = true;
    QString local = QString::fromLocal8Bit(qgetenv("QTSCRCPY_SERVER_PATH"));
    if (!QFileInfo(local).isFile()) local = QCoreApplication::applicationDirPath() + "/scrcpy-server";
    if (!QFileInfo(local).isFile()) { namesFailed(tr("找不到运行包中的 scrcpy-server")); return; }
    m_queryServerPath = "/data/local/tmp/qtscrcpy-app-labels-"
        + QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-') + ".jar";
    m_commands->run("names-push", m_serial, {"push", QFileInfo(local).absoluteFilePath(), m_queryServerPath}, 15000);
}
void AppSession::namesFailed(const QString &error) {
    m_namesPending = false;
    const auto text = tr("应用名称读取失败，已保留缓存；可在 + 中刷新。%1").arg(error.left(240));
    qWarning().noquote() << text;
    if (!locked()) setStatus(text);
    emit appsChanged();
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
            m_stable.invalidate(); setStatus(tr("前台检测暂时无响应，已暂停输入…"));
        }
    }
    if (!m_probePending) {
        m_probePending = true;
        m_commands->run("focus", m_serial, {"shell", "dumpsys window | grep mCurrentFocus"}, 2500);
    }
}
void AppSession::refreshFocus() {
    // A pre-navigation probe must not authorize resuming input after a launch.
    m_commands->cancel("focus"); m_probePending = false;
    m_stable.invalidate(); tick();
}
void AppSession::result(const QString &tag, bool success, const QString &output, const QString &error) {
    if (!m_connected) return;
    if (tag == "identity") {
        const auto identity = output.trimmed();
        loadProfiles(success && !identity.isEmpty() && identity != "unknown" ? identity : m_serial);
        applyForegroundKeymap(); emit appsChanged();
    } else if (tag == "catalog") {
        const auto parsed = parseComponents(output);
        if (!success || parsed.isEmpty()) { setStatus(tr("读取应用列表失败，可点击 + 重试。%1").arg(error)); return; }
        QHash<QString, PhoneApp> updated;
        for (auto app : parsed) {
            if (m_apps.contains(app.packageName)) app.label = m_apps.value(app.packageName).label;
            updated.insert(app.packageName, app);
        }
        m_apps = updated; ensureTab(m_foreground); emit appsChanged(); requestNames();
    } else if (tag == "names-push") {
        if (!m_namesPending) return;
        if (!success) { namesFailed(error); return; }
        m_commands->run("names", m_serial, {"shell", "CLASSPATH=" + m_queryServerPath, "app_process", "/",
            "com.genymobile.scrcpy.Server", qsc::DeviceParams().serverVersion,
            "list_apps=true", "cleanup=true", "log_level=info"}, 15000);
    } else if (tag == "names") {
        if (!m_namesPending) return;
        const auto labels = success ? AppLabels::parse(output) : QHash<QString, QString>();
        int accepted = 0;
        for (auto it = labels.begin(); it != labels.end(); ++it) {
            if (!m_apps.contains(it.key())) continue;
            m_apps[it.key()].label = it.value(); m_labels[it.key()] = it.value(); ++accepted;
            if (m_profiles.contains(it.key())) m_profiles[it.key()]["label"] = it.value();
        }
        if (!accepted && !m_namesStaged) { stageNameQuery(); return; }
        if (!accepted) { namesFailed(error.isEmpty() ? tr("查询未返回可识别的应用名称") : error); return; }
        m_namesPending = false;
        // Persist only verified labels, scoped by physical device identity.
        saveProfiles(); emit appsChanged();
    } else if (tag == "focus") {
        m_probePending = false;
        if (success) m_lastProbe.restart();
        observe(success ? parseForeground(output) : QString()); advanceGuard();
        // A system-panel gesture may temporarily own the input channel when
        // foreground changes. Retry after it ends, even if focus is unchanged.
        if (!locked()) applyForegroundKeymap();
    } else if (tag == "launch") {
        const bool navigation = m_launchIsNavigation;
        m_launchPending = false; m_launchIsNavigation = false;
        if (!success || output.contains("Error:") || output.contains("Exception")) {
            const auto message = tr("应用启动失败：%1").arg((output + " " + error).trimmed().left(240));
            // Failure to visit another app must not discard the bound macro.
            if (locked() && !navigation) { failGuard(message); return; }
            setStatus(message); emit failure(message);
        }
        if (m_navigationPending) dispatchNavigation();
        refreshFocus(); m_recentTasks->refresh();
    }
}
void AppSession::observe(const QString &packageName) {
    if (m_foreground == packageName) return;
    m_foreground = packageName; m_stable.invalidate(); ensureTab(packageName);
    if (!locked()) applyForegroundKeymap();
    emit foregroundChanged(packageName);
    if (!locked()) setStatus(packageName.isEmpty() ? tr("手机桌面、锁屏或系统界面") : tr("当前：%1").arg(label(packageName)));
}
void AppSession::ensureTab(const QString &packageName) {
    if (!m_profilesReady || !m_apps.contains(packageName) || m_tabs.contains(packageName) || m_tabs.size() >= 32) return;
    m_tabs.append(packageName); saveProfiles(); emit appsChanged();
}
void AppSession::activate(const QString &packageName) {
    if (!m_connected || !m_device || closingApp()) return;
    if (m_device->isActionRecording()) { emit failure(tr("请先结束录制再切换应用。")); return; }
    if (!packageName.isEmpty() && (!AppBinding::validPackage(packageName) || !m_apps.contains(packageName)
                                  || m_apps.value(packageName).component.isEmpty())) {
        emit failure(tr("应用未安装或没有启动入口：%1").arg(label(packageName))); return;
    }
    if (locked() && !m_manualPaused) {
        // Navigation stays enabled. Suspend input BEFORE sending the launch,
        // keep its original target, and let the existing guard bring it back.
        if (m_started && !m_device->isActionPaused()) {
            m_internal = true; const bool paused = m_device->pauseActionMacro(); m_internal = false;
            if (!paused) { failGuard(tr("无法暂停预制操作，已停止，未切换应用。")); return; }
        }
        m_autoPaused = m_started;
        m_stable.invalidate();
        if (!m_recovery.isValid()) { m_recovery.start(); m_launchAttempts = 0; }
        setStatus(tr("正在切换应用；预制操作稍后会自动切回 %1").arg(label(m_target)));
    }
    if (!packageName.isEmpty()) ensureTab(packageName);
    // Serialize launch commands; rapid clicks keep the last requested tab.
    // Never kill/restart the live scrcpy process or clear the macro binding.
    m_navigationTarget = packageName; m_navigationPending = true;
    dispatchNavigation();
}
void AppSession::dispatchNavigation() {
    if (!m_connected || !m_navigationPending || m_launchPending) return;
    const QString packageName = m_navigationTarget;
    m_navigationPending = false; m_navigationTarget.clear(); m_stable.invalidate();
    m_launchPending = true; m_launchIsNavigation = true;
    if (packageName.isEmpty()) {
        m_commands->run("launch", m_serial, {"shell", "am", "start", "-a", "android.intent.action.MAIN",
                                            "-c", "android.intent.category.HOME"}, 8000);
    } else {
        const auto app = m_apps.value(packageName);
        if (app.component.isEmpty()) {
            m_launchPending = m_launchIsNavigation = false;
            emit failure(tr("应用已从列表移除，请刷新应用列表。")); refreshFocus(); return;
        }
        m_commands->run("launch", m_serial, {"shell", "am", "start", "--user", "current", "-a", "android.intent.action.MAIN",
            "-c", "android.intent.category.LAUNCHER", "-f", "0x10200000", "-n", "'" + app.component + "'"}, 8000);
    }
}
void AppSession::launch(const QString &packageName) {
    if (m_launchPending || m_navigationPending) return;
    const auto app = m_apps.value(packageName);
    if (!AppBinding::validPackage(packageName) || app.component.isEmpty()) {
        const auto message = tr("应用未安装或没有启动入口：%1").arg(label(packageName));
        if (locked()) failGuard(message); else emit failure(message);
        return;
    }
    m_launchPending = true; m_launchIsNavigation = false;
    if (locked()) ++m_launchAttempts;
    setStatus(tr("正在切换到 %1…").arg(label(packageName)));
    m_commands->run("launch", m_serial, {"shell", "am", "start", "--user", "current", "-a", "android.intent.action.MAIN",
        "-c", "android.intent.category.LAUNCHER", "-f", "0x10200000", "-n", "'" + app.component + "'"}, 8000);
}
void AppSession::closeTab(const QString &packageName) {
    if (packageName == m_target) return;
    m_tabs.removeAll(packageName); saveProfiles(); emit appsChanged();
}
QString AppSession::keymap(const QString &packageName) const { return m_profiles.value(packageName).value("keymap").toString(); }
bool AppSession::bindKeymap(const QString &packageName, const QString &script) {
    if (!ready() || !AppBinding::validPackage(packageName)) return false;
    KeymapDocument document; QString error;
    if (!script.isEmpty() && (script.toUtf8().size() > 1024 * 1024 || !document.parse(script.toUtf8(), &error))) return false;
    const auto original = m_profiles;
    auto &profile = m_profiles[packageName]; profile["label"] = label(packageName); profile["keymap"] = script;
    if (!saveProfiles()) { m_profiles = original; return false; }
    m_hasAppKeymap = true; m_appliedPackage.clear(); m_appliedScript.clear();
    ensureTab(packageName); applyForegroundKeymap(); emit appsChanged(); return true;
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
    if (!m_hasAppKeymap && script.isEmpty()) return;
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
    m_recovery.start(); m_stable.invalidate(); ensureTab(m_target); emit lockChanged();
    launch(m_target); tick();
}
void AppSession::advanceGuard() {
    if (!locked() || m_manualPaused || !m_device || m_launchPending || m_navigationPending) return;
    if (m_foreground != m_target) {
        m_stable.invalidate();
        if (m_started && !m_device->isActionPaused()) {
            m_internal = true; m_autoPaused = m_device->pauseActionMacro(); m_internal = false;
            if (!m_autoPaused) { failGuard(tr("无法暂停预制操作，已停止。")); return; }
        }
        if (!m_recovery.isValid()) { m_recovery.start(); m_launchAttempts = 0; }
        if (m_launchAttempts < 3 && m_recovery.elapsed() >= m_launchAttempts * 2000) launch(m_target);
        return;
    }
    if (!m_stable.isValid()) { m_stable.start(); return; }
    if (m_stable.elapsed() < 500) return;
    if (!m_device->actionMacroScreenMatches()) {
        setStatus(tr("等待 %1 恢复录制时的画面尺寸和方向…").arg(label(m_target))); return;
    }
    if (m_pendingStart) {
        const auto play = m_pendingStart; m_pendingStart = nullptr;
        m_internal = true; m_started = true; const bool ok = play(); m_internal = false;
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
    setStatus(tr("预制操作运行中 · 可切换标签，将自动切回 %1").arg(label(m_target)));
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
    if (m_launchPending && !m_launchIsNavigation) {
        m_commands->cancel("launch"); m_launchPending = false;
        dispatchNavigation(); refreshFocus();
    }
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
    m_commands->cancel("launch"); m_launchPending = m_launchIsNavigation = false;
    m_navigationPending = false; m_navigationTarget.clear();
    m_target.clear(); m_pendingStart = nullptr; m_started = m_autoPaused = m_manualPaused = false;
    m_recovery.invalidate(); m_stable.invalidate();
    if (m_device) { m_device->setActionMacroApplicationBound(false); m_device->stopActionPlayback(); }
    applyForegroundKeymap();
    setStatus(!m_connected ? tr("手机已断开") : m_foreground.isEmpty() ? tr("手机桌面、锁屏或系统界面") : tr("当前：%1").arg(label(m_foreground)));
    emit lockChanged();
}
void AppSession::failGuard(const QString &text) { stopMacro(); setStatus(text); emit failure(text); }
void AppSession::setStatus(const QString &text) { if (m_status != text) { m_status = text; emit statusChanged(text); } }
void AppSession::loadProfiles(const QString &identity) {
    const auto hash = QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex();
    m_profilePath = QDir(m_directory).filePath(QString::fromLatin1(hash) + ".json"); m_profilesReady = true;
    QFile file(m_profilePath);
    if (!file.exists()) { ensureTab(m_foreground); return; }
    bool ok = file.open(QIODevice::ReadOnly) && file.size() <= 16 * 1024 * 1024;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(ok ? file.readAll() : QByteArray(), &error);
    const auto root = document.object();
    ok = ok && error.error == QJsonParseError::NoError && root.value("version").toDouble() == 1
         && root.value("profiles").isObject() && root.value("tabs").isArray() && root.value("profiles").toObject().size() <= 128;
    QHash<QString, QString> savedLabels;
    if (ok && root.contains("labels")) {
        ok = root["labels"].isObject() && root["labels"].toObject().size() <= 1024;
        const auto labels = root["labels"].toObject();
        for (auto it = labels.begin(); ok && it != labels.end(); ++it) {
            ok = AppBinding::validPackage(it.key()) && it.value().isString() && AppLabels::usable(it.value().toString(), it.key());
            if (ok) savedLabels.insert(it.key(), it.value().toString());
        }
    }
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
        setStatus(tr("应用配置文件无效，已保留原文件并禁止覆盖：%1").arg(m_profilePath)); emit failure(m_status);
    } else {
        for (auto it = savedLabels.begin(); it != savedLabels.end(); ++it)
            if (!m_labels.contains(it.key())) m_labels.insert(it.key(), it.value());
    }
    ensureTab(m_foreground);
}
bool AppSession::saveProfiles() {
    if (!m_profilesReady || m_readOnlyProfiles || m_profiles.size() > 128 || !QDir().mkpath(m_directory)) return false;
    QJsonObject profiles, labels;
    for (auto it = m_profiles.begin(); it != m_profiles.end(); ++it) profiles[it.key()] = it.value();
    for (auto it = m_labels.begin(); it != m_labels.end() && labels.size() < 1024; ++it) {
        if (AppLabels::usable(it.value(), it.key())) labels[it.key()] = it.value();
    }
    const auto data = QJsonDocument(QJsonObject{{"version", 1}, {"tabs", QJsonArray::fromStringList(m_tabs)},
                                               {"profiles", profiles}, {"labels", labels}}).toJson();
    if (data.size() > 16 * 1024 * 1024) return false;
    QSaveFile file(m_profilePath);
    const bool ok = file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
    if (!ok) emit failure(tr("应用配置保存失败：%1").arg(m_profilePath));
    return ok;
}
