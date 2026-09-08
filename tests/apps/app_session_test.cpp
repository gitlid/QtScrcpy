#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QJsonArray>
#include <QLabel>
#include <QTabBar>
#include <QTemporaryDir>
#include <QToolButton>
#include <QVBoxLayout>
#include <stdexcept>
#include <cstdio>
#include "../../QtScrcpy/ui/appsession.h"
#include "../../QtScrcpy/ui/appbar.h"
#include "../../QtScrcpy/ui/actionmacrohotkey.h"
#include "../../QtScrcpy/ui/videoinputgeometry.h"

namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
void wait(int ms) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); }
const QString first = "com.example.game", second = "com.example.tools";
const QString catalog = first + "/.Main\n" + second + "/.Main\n";
QString mapping(int x) {
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"switchKey", "Key_QuoteLeft"}, {"mouseLookEnabled", false},
        {"keyMapNodes", QJsonArray{QJsonObject{{"type", "KMT_CLICK"}, {"key", "Key_A"}, {"switchMap", false},
        {"pos", QJsonObject{{"x", x / 100.0}, {"y", .5}}}}}}}).toJson());
}
class FakeDevice final : public qsc::IDevice {
public:
    explicit FakeDevice(const QString &transport = QStringLiteral("192.0.2.1:5555")) : serial(transport) {}
    QString serial = "192.0.2.1:5555", script;
    bool playing = false, paused = false, recording = false;
    bool matchingScreen = true, applicationBound = false;
    int plays = 0, pauses = 0, resumes = 0;
    QStringList applied;
    void changed() { emit actionMacroStateChanged(recording, playing, 4); }
    void setUserData(void *) override {} void *getUserData() override { return nullptr; }
    void registerDeviceObserver(qsc::DeviceObserver *) override {} void deRegisterDeviceObserver(qsc::DeviceObserver *) override {}
    bool connectDevice() override { return true; } void disconnectDevice() override { emit deviceDisconnected(serial); }
    void mouseEvent(const QMouseEvent *, const QSize &, const QSize &) override {}
    void wheelEvent(const QWheelEvent *, const QSize &, const QSize &) override {}
    void keyEvent(const QKeyEvent *, const QSize &, const QSize &) override {}
    void postGoBack() override {} void postGoHome() override {} void postGoMenu() override {} void postAppSwitch() override {}
    void postPower() override {} void postVolumeUp() override {} void postVolumeDown() override {}
    void postCopy() override {} void postCut() override {} void setDisplayPower(bool) override {}
    void expandNotificationPanel() override {} void expandSettingsPanel() override {} void collapsePanel() override {}
    void rotateDevice() override {} void startApp(const QString &) override {} void resizeDisplay(const QSize &) override {}
    void postBackOrScreenOn(bool) override {} void postTextInput(QString &) override {} void requestDeviceClipboard() override {}
    void setDeviceClipboard(bool) override {} void clipboardPaste() override {} void pushFileRequest(const QString &, const QString &) override {}
    void installApkRequest(const QString &) override {} void screenshot() override {} void showTouch(bool) override {}
    bool isReversePort(quint16) override { return false; } const QString &getSerial() override { return serial; }
    void updateScript(QString value) override { script = value; } bool isCurrentCustomKeymap() override { return !script.isEmpty(); }
    bool applyAppKeymap(const QString &value) override { script = value; applied.append(value); return true; }
    void setActionMacroApplicationBound(bool value) override { applicationBound = value; }
    bool actionMacroScreenMatches() const override { return matchingScreen; }
    bool isUhidKeyboardEnabled() const override { return true; } void releaseKeyboard() override {}
    bool startActionRecording() override { recording = true; changed(); return true; }
    bool stopActionRecording() override { recording = false; changed(); return true; }
    bool saveActionMacro(const QString &, QString *) const override { return true; }
    bool loadActionMacro(const QString &, QString *) override { return true; }
    bool playActionMacro(int, int) override { playing = true; paused = false; ++plays; changed(); return true; }
    bool playActionMacroAdvanced(int r, int i, double, qint64) override { return playActionMacro(r, i); }
    bool pauseActionMacro() override { if (!playing || paused) return false; paused = true; ++pauses; changed(); return true; }
    bool resumeActionMacro() override { if (!playing || !paused) return false; paused = false; ++resumes; changed(); return true; }
    bool isActionPaused() const override { return paused; }
    void stopActionPlayback() override { playing = paused = false; changed(); }
    bool isActionRecording() const override { return recording; } bool isActionPlaying() const override { return playing; }
    int actionMacroEventCount() const override { return 4; }
};
class FakeCommands final : public AppCommands {
public:
    struct Request { QString tag, serial; QStringList args; };
    QList<Request> requests;
    QSet<QString> pending;
    void run(const QString &tag, const QString &serial, const QStringList &args, int) override {
        if (pending.contains(tag)) return;
        requests.append({tag, serial, args}); pending.insert(tag);
    }
    void cancelAll() override { pending.clear(); }
    void cancel(const QString &tag) override { pending.remove(tag); }
    void complete(const QString &tag, const QString &output = QString(), bool ok = true) {
        require(pending.remove(tag), "request must be pending"); emit finished(tag, ok, output, ok ? QString() : "failure");
    }
    int launches() const { int count = 0; for (const auto &r : requests) if (r.tag == "launch") ++count; return count; }
};
struct Fixture {
    QTemporaryDir directory;
    FakeDevice device;
    FakeCommands commands;
    AppSession session;
    QStringList errors;
    explicit Fixture(const QString &root = QString(), const QString &transport = QStringLiteral("192.0.2.1:5555"))
        : device(transport), session(&device, "/data/local/tmp/scrcpy-server.jar", nullptr, &commands, root.isEmpty() ? directory.path() : root) {
        QObject::connect(&session, &AppSession::failure, &session, [this](const QString &s) { errors.append(s); });
        session.start();
        commands.complete("identity", "stable-phone-id\n");
        commands.complete("catalog", catalog);
        commands.complete("names", QString::fromUtf8(" * 地球末日：生存    com.example.game\n - MT管理器    com.example.tools\n"));
    }
    void focus(const QString &packageName) {
        if (!commands.pending.contains("focus")) wait(270);
        const auto output = packageName.isEmpty() ? QStringLiteral("mCurrentFocus=Window{abc u0 NotificationShade}")
            : "mCurrentFocus=Window{abc u0 " + packageName + "/.Main}";
        commands.complete("focus", output);
    }
    void settle(const QString &packageName) { focus(packageName); wait(270); focus(packageName); wait(270); focus(packageName); }
    void start() {
        session.startMacro(session.binding(first), [this] { return device.playActionMacro(1, 0); });
        commands.complete("launch", "Starting: Intent");
        settle(first);
        require(device.playing, "verified target must start playback");
    }
};
void parsers() {
    const auto apps = AppSession::parseComponents(catalog + first + "/.Other\ncom.bad;touch/.Main\ncom.example.nested/.Main$Launcher\n");
    require(apps.size() == 3, "component parser filters duplicates and shell text");
    require(AppSession::parseForeground("mCurrentFocus=Window{aa u10 com.example.game/.Main}") == first, "foreground package");
    require(AppSession::parseForeground("mCurrentFocus=Window{aa u0 NotificationShade}\nmFocusedApp=ActivityRecord{aa com.example.game/.Main}").isEmpty(), "system overlay cannot reuse background activity");
    require(!AppBinding::validPackage("com.example.game\nam start") && !AppBinding::validPackage("../game"), "package validation");
}
void metadata() {
    QTemporaryDir directory; const QString path = directory.filePath("macro.json");
    QFile file(path); require(file.open(QIODevice::WriteOnly), "write macro");
    file.write("{\"format\":\"QtScrcpyActionMacro\",\"version\":2,\"events\":[{\"atMs\":10}],\"executionSettings\":{\"speed\":8}}"); file.close();
    AppBinding bound; require(AppBinding::load(path, &bound) && bound.isEmpty(), "old unbound macro stays compatible");
    bound.packageName = first; bound.label = QString::fromUtf8("游戏");
    require(AppBinding::save(path, bound), "save binding"); AppBinding loaded;
    require(AppBinding::load(path, &loaded) && loaded.packageName == first && loaded.label == bound.label, "binding roundtrip");
    file.open(QIODevice::ReadOnly); auto root = QJsonDocument::fromJson(file.readAll()).object(); file.close();
    require(root["version"].toInt() == 2 && root["events"].toArray().size() == 1 && root["executionSettings"].toObject()["speed"].toInt() == 8, "preserve events and speed");
    require(AppBinding::save(path, AppBinding()) && AppBinding::load(path, &loaded) && loaded.isEmpty(), "clear binding");
    require(!AppBinding::parse(QJsonArray(), &loaded), "malformed binding rejected");
}
void profileSwitch() {
    Fixture f; f.focus(first);
    require(f.session.bindKeymap(first, mapping(20)), "bind first mapping");
    require(f.device.script == mapping(20), "current app mapping applies immediately");
    require(f.session.bindKeymap(second, mapping(70)), "bind second mapping");
    f.focus(second); require(f.device.script == mapping(70), "switch applies second mapping");
    f.focus("com.unknown.app"); require(f.device.script.isEmpty(), "unbound app clears app mapping");
    f.focus(first); require(f.device.script == mapping(20), "return restores first mapping");
}
void profileRestart() {
    QTemporaryDir root;
    { Fixture f(root.path()); f.focus(first); require(f.session.bindKeymap(first, mapping(30)), "save profile");
      require(f.session.rememberMacro(f.session.binding(first), root.filePath("demo.json")), "save preset path"); }
    Fixture g(root.path(), "192.0.2.9:5555"); g.focus(first);
    require(g.session.tabs().contains(first) && g.device.script == mapping(30), "stable hardware identity restores tab and mapping");
    require(g.session.macros(first).contains(root.filePath("demo.json")), "preset index persists");
}
void corruptProfile() {
    QTemporaryDir root; const auto hash = QCryptographicHash::hash("stable-phone-id", QCryptographicHash::Sha256).toHex();
    QFile file(root.filePath(QString::fromLatin1(hash) + ".json")); file.open(QIODevice::WriteOnly); file.write("bad json"); file.close();
    Fixture f(root.path()); require(!f.session.bindKeymap(first, mapping(20)), "corrupt file must not be overwritten");
    file.open(QIODevice::ReadOnly); require(file.readAll() == "bad json", "keep corrupt source for recovery");
}
void launchBeforePlay() {
    Fixture f; f.focus(second);
    f.session.startMacro(f.session.binding(first), [&f] { return f.device.playActionMacro(1, 0); });
    require(!f.device.playing && f.session.preparing(), "no input before launch confirmation");
    require(f.commands.requests.last().serial == f.device.serial, "launch is scoped to selected phone");
    f.commands.complete("launch", "Starting: Intent"); f.focus(second);
    require(!f.device.playing, "launch success alone does not permit replay");
    f.settle(first); require(f.device.plays == 1 && !f.session.preparing(), "start exactly once after stable target");
}
void switchBack() {
    Fixture f; f.start(); const int before = f.commands.launches();
    f.focus(second); require(f.device.paused && f.device.pauses == 1, "switch pauses and releases input");
    require(f.commands.launches() == before + 1, "switch relaunches bound application");
    f.commands.complete("launch", "Warning: Activity not started, task brought to front");
    f.settle(first); require(!f.device.paused && f.device.resumes == 1 && f.device.plays == 1, "continue instead of restarting macro");
    require(f.session.locked(), "lock remains while playback runs");
}
void stopPending() {
    Fixture f;
    f.session.startMacro(f.session.binding(first), [&f] { return f.device.playActionMacro(1, 0); });
    f.session.stopMacro(); require(!f.commands.pending.contains("launch"), "stop cancels launch client");
    f.settle(first); require(f.device.plays == 0 && !f.session.locked(), "late focus cannot restart a stopped macro");
}
void manualPause() {
    Fixture f; f.start(); f.session.pauseMacro(); const int launches = f.commands.launches();
    f.focus(second); require(f.device.paused && f.commands.launches() == launches, "manual pause does not force app back");
    f.session.resumeMacro(); require(f.device.paused && f.commands.launches() == launches + 1, "resume reacquires target first");
    f.commands.complete("launch", "Starting: Intent"); f.settle(first); require(!f.device.paused, "resume after verification");
}
void missingApp() {
    Fixture f; AppBinding bound; bound.packageName = "com.missing.game"; bound.label = "missing";
    f.session.startMacro(bound, [&f] { return f.device.playActionMacro(1, 0); });
    require(!f.session.locked() && f.device.plays == 0 && !f.errors.isEmpty(), "missing app never runs input");
}
void launchFailure() {
    Fixture f;
    f.session.startMacro(f.session.binding(first), [&f] { return f.device.playActionMacro(1, 0); });
    f.commands.complete("launch", "Error: Activity class does not exist", true);
    require(!f.session.locked() && f.device.plays == 0 && !f.errors.isEmpty(), "am error text rejects apparent success");
}
void failedProbe() {
    Fixture f; f.start(); if (!f.commands.pending.contains("focus")) wait(270);
    f.commands.complete("focus", QString(), false);
    require(f.device.paused, "query failure pauses playback");
    f.session.stopMacro(); f.focus(second); require(!f.session.locked(), "failure cannot revive stopped macro");
}
void disconnect() {
    Fixture f; f.start(); f.device.disconnectDevice();
    require(!f.device.playing && !f.session.locked() && f.commands.pending.isEmpty(), "disconnect cleans timers and children");
    const int requests = f.commands.requests.size(); wait(300); require(f.commands.requests.size() == requests, "no polling after disconnect");
}
void hotkeyPending() {
    Fixture f;
    auto *keys = ActionMacroHotkey::instance();
    keys->watchControls(&f.session, [&f] { f.session.stopMacro(); }, [&f] { f.session.pauseMacro(); });
    f.session.startMacro(f.session.binding(first), [&f] { return f.device.playActionMacro(1, 0); });
    keys->stopAll(); f.settle(first); require(f.device.plays == 0 && !f.session.locked(), "global emergency stop cancels acquisition");
}
void geometryRestore() {
    Fixture f; f.start(); require(f.device.applicationBound, "bound replay enables temporary geometry pause");
    f.device.matchingScreen = false; emit f.device.actionMacroApplicationInterrupted(); f.device.pauseActionMacro();
    f.focus(second); f.commands.complete("launch", "Starting: Intent");
    f.settle(first); require(f.device.paused && f.device.resumes == 0, "wait for recorded dimensions after app returns");
    f.device.matchingScreen = true; f.focus(first); require(!f.device.paused && f.device.resumes == 1, "resume only after dimensions restore");
    f.session.stopMacro(); require(!f.device.applicationBound, "stop clears the temporary geometry policy");
}
void stalledProbe() {
    Fixture f; f.start(); wait(1200);
    require(f.device.paused && f.session.locked(), "stalled foreground query cannot leave replay running");
}
void unreachable() {
    Fixture f; f.focus(second);
    f.session.startMacro(f.session.binding(first), [&f] { return f.device.playActionMacro(1, 0); });
    QElapsedTimer timer; timer.start();
    while (f.session.locked() && timer.elapsed() < 16500) {
        if (f.commands.pending.contains("launch")) f.commands.complete("launch", "Starting: Intent");
        f.focus(second); wait(260);
    }
    require(!f.session.locked() && f.device.plays == 0 && f.commands.launches() <= 3 && !f.errors.isEmpty(), "unreachable target stops with bounded retries");
}
void mapAfterPlayback() {
    Fixture f; f.focus(first); f.session.bindKeymap(first, mapping(20)); f.session.bindKeymap(second, mapping(70));
    f.device.playActionMacro(1, 0); f.focus(second);
    require(f.device.script == mapping(20), "profile must not replace a mapper owned by playback");
    f.device.stopActionPlayback(); require(f.device.script == mapping(70), "completion applies the current application's profile");
}
void tabsLayout() {
    Fixture f; f.focus(first); f.focus(second);
    AppBar bar(&f.session); bar.resize(720, 66); bar.show(); wait(30);
    auto *tabs = bar.findChild<QTabBar *>("phoneAppTabs");
    require(tabs && tabs->count() == 3 && tabs->tabData(tabs->currentIndex()).toString() == second, "foreground tab is selected");
    require(bar.findChild<QToolButton *>("addPhoneApp")->isEnabled(), "application picker available");
    bar.resize(380, 66); wait(30); require(tabs->width() > 50 && tabs->geometry().right() < bar.width(), "portrait tabs fit with scrolling");
    if (qEnvironmentVariableIsSet("QSC_APP_SCREENSHOT")) { bar.resize(950,66); wait(30); bar.grab().save(qEnvironmentVariable("QSC_APP_SCREENSHOT")); }
}
void videoHitArea() {
    QWidget window; window.resize(840, 720);
    QWidget container(&window); container.setGeometry(10, 80, 820, 600);
    QWidget video(&container); video.setGeometry(10, 0, 800, 600);
    require(VideoInputGeometry::contains(&window, &video, QPoint(20, 80)), "top-left video point includes container offset");
    require(VideoInputGeometry::contains(&window, &video, QPoint(819, 679)), "bottom edge remains clickable below the tabs");
    require(!VideoInputGeometry::contains(&window, &video, QPoint(100, 30)), "application tabs are outside phone input");
    require(!VideoInputGeometry::contains(&window, &video, QPoint(820, 680)), "right and bottom bounds are excluded");
}
}
int main(int argc, char **argv) {
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &text) { std::fprintf(stderr, "%s\n", text.toUtf8().constData()); });
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    const QVector<QPair<QString, std::function<void()>>> cases{
        {"parsers", parsers}, {"binding_metadata", metadata}, {"profile_switch", profileSwitch}, {"profile_restart", profileRestart},
        {"corrupt_profile", corruptProfile}, {"launch_before_play", launchBeforePlay}, {"switch_back", switchBack},
        {"stop_pending", stopPending}, {"manual_pause", manualPause}, {"missing_app", missingApp}, {"launch_failure", launchFailure},
        {"failed_probe", failedProbe}, {"disconnect", disconnect}, {"hotkey_pending", hotkeyPending}, {"tabs_layout", tabsLayout},
        {"geometry_restore", geometryRestore}, {"stalled_probe", stalledProbe}, {"unreachable", unreachable}, {"map_after_playback", mapAfterPlayback}, {"video_hit_area", videoHitArea}};
    int ran = 0, failed = 0;
    for (const auto &test : cases) {
        if (argc > 1 && QString::fromLocal8Bit(argv[1]) != test.first) continue;
        ++ran;
        try { test.second(); qInfo("PASS %s", qPrintable(test.first)); }
        catch (const std::exception &error) { ++failed; qCritical("FAIL %s: %s", qPrintable(test.first), error.what()); }
    }
    return ran > 0 && failed == 0 ? 0 : 1;
}
