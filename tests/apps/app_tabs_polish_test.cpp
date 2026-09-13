// Reuse the existing fake-device/command fixtures, not a second model of the
// production session. Its original suite remains a separate CTest executable.
#define main existing_app_session_test_main
#include "app_session_test.cpp"
#undef main
#include <QAction>
#include <QMenu>
#include "../../QtScrcpy/ui/applabels.h"
#include "../../QtScrcpy/ui/appcommandprocess.h"

namespace {
struct EnvironmentValue {
    QByteArray name, previous; bool existed;
    EnvironmentValue(const char *key, const QByteArray &value) : name(key), previous(qgetenv(key)), existed(qEnvironmentVariableIsSet(key)) { qputenv(key, value); }
    ~EnvironmentValue() { if (existed) qputenv(name.constData(), previous); else qunsetenv(name.constData()); }
};
void nameUtf8() {
    const QString text = QString::fromUtf8(" * 微信    com.example.game\r\n - R&D 测试😀    com.example.tools\n");
    AppCommandOutput output; const QByteArray bytes = text.toUtf8();
    for (const char byte : bytes) require(output.append(QByteArray(1, byte)), "append raw UTF-8 byte");
    require(output.standard() == text, "Chinese/emoji and line boundaries survive arbitrary packet splits");
    const auto labels = AppLabels::parse(output.standard());
    require(labels.value(first) == QString::fromUtf8("微信") && labels.value(second) == QString::fromUtf8("R&D 测试😀"), "actual names parsed from stdout");
}
void nameWrapped() {
    const QString longName = QString(40, QChar('A')) + QString::fromUtf8(" 测试");
    const auto labels = AppLabels::parse("[server] INFO: List of apps:\n - " + longName + "\n                                 " + first + "\n * Settings   " + second + "\n");
    require(labels.value(first) == longName && labels.value(second) == "Settings", "wrapped and one-line listing formats");
}
void nameDiagnostics() {
    const auto labels = AppLabels::parse("ERROR: Missing package com.fake.game\ncom.example.game\n * Real App    com.example.tools\n - unpaired label\nERROR\ncom.bad.app\n");
    require(labels.size() == 1 && labels.value(second) == "Real App", "diagnostics cannot become friendly labels");
}
void outputChannels() {
    AppCommandOutput output;
    require(output.append(" - ", true) && output.append(QString::fromUtf8("设置").toUtf8(), true)
        && output.append("    com.example.game\n", true), "stderr chunks");
    require(AppLabels::parse(output.errors()).value(first) == QString::fromUtf8("设置"), "names can arrive on stderr");
    require(output.append(QByteArray(4 * 1024 * 1024, 'x')) == false, "bounded combined output");
}
void cachedWithoutBinding() {
    QTemporaryDir root;
    { Fixture f(root.path()); f.focus(first); }
    FakeDevice device("192.0.2.42:5555"); FakeCommands commands;
    AppSession session(&device, "/data/local/tmp/scrcpy-server.jar", nullptr, &commands, root.path());
    session.start(); commands.complete("identity", "stable-phone-id\n"); commands.complete("catalog", catalog);
    require(session.label(first) == QString::fromUtf8("地球末日：生存"), "unbound tab remembers actual name across transport changes");
    require(session.keymap(first).isEmpty(), "label cache does not invent a keymap binding");
}
void cacheDeviceIsolation() {
    QTemporaryDir root;
    { Fixture f(root.path()); f.focus(first); }
    FakeDevice device; FakeCommands commands;
    AppSession session(&device, "/data/local/tmp/scrcpy-server.jar", nullptr, &commands, root.path());
    session.start(); commands.complete("identity", "another-phone\n"); commands.complete("catalog", catalog);
    require(session.label(first) == QString::fromUtf8("名称未读取"), "no label from another phone and no package-as-label");
}
void nameRetryCopy() {
    Fixture f;
    QTemporaryDir local; QFile server(local.filePath("scrcpy-server"));
    require(server.open(QIODevice::WriteOnly), "create fake server fixture"); server.write("fixture"); server.close();
    EnvironmentValue environment("QTSCRCPY_SERVER_PATH", QFile::encodeName(server.fileName()));
    f.session.refreshApps(); f.commands.complete("catalog", catalog);
    f.commands.complete("names", "ClassNotFoundException", false);
    require(f.commands.pending.contains("names-push"), "failed live JAR lookup stages an independent copy");
    const auto request = f.commands.requests.last();
    require(request.args.first() == "push" && request.args.last().startsWith("/data/local/tmp/qtscrcpy-app-labels-")
        && request.args.last() != "/data/local/tmp/scrcpy-server.jar", "never overwrite the live server path");
    f.commands.complete("names-push");
    require(f.commands.requests.last().args.contains("cleanup=true") && f.commands.requests.last().args.contains("list_apps=true"), "query-only copy cleans itself and never starts mirroring");
    f.commands.complete("names", QString::fromUtf8(" - 新名称    com.example.game\n - 工具    com.example.tools\n"));
    require(f.session.label(first) == QString::fromUtf8("新名称"), "verified names replace old cache");
}
void nameRetryFailure() {
    Fixture f;
    QTemporaryDir local; QFile server(local.filePath("scrcpy-server")); server.open(QIODevice::WriteOnly); server.write("fixture"); server.close();
    EnvironmentValue environment("QTSCRCPY_SERVER_PATH", QFile::encodeName(server.fileName()));
    f.session.refreshApps(); f.commands.complete("catalog", catalog); f.commands.complete("names", QString(), false);
    f.commands.complete("names-push", QString(), false);
    require(f.session.label(first) == QString::fromUtf8("地球末日：生存"), "failed name refresh keeps verified cache");
    require(f.session.status().contains(QString::fromUtf8("名称读取失败")), "failure is visible, not silent package fallback");
}
void ordinaryTabSwitch() {
    Fixture f; f.focus(first); f.session.activate(second);
    require(f.commands.pending.contains("launch") && f.commands.requests.last().args.last().contains(second), "normal tab navigation remains available");
    f.commands.complete("launch"); f.settle(second);
    require(!f.session.locked() && !f.device.playing && f.commands.launches() == 1, "unbound navigation does not create a macro guard");
}
void macroTabSwitch() {
    Fixture f; f.start(); const int before = f.commands.launches();
    f.session.activate(second);
    require(f.device.paused && f.session.locked() && f.commands.launches() == before + 1, "pause before navigating without clearing target");
    require(f.commands.requests.last().args.last().contains(second), "clicked tab is actually launched");
    f.focus(first); require(f.device.paused && f.device.resumes == 0, "stale target observation while launching cannot resume");
    f.commands.complete("launch"); f.focus(second);
    require(f.commands.launches() == before + 2 && f.commands.requests.last().args.last().contains(first), "guard returns to original bound app");
    f.commands.complete("launch"); f.settle(first);
    require(!f.device.paused && f.device.resumes == 1 && f.device.plays == 1, "resume remaining macro, not restart");
}
void repeatedVisits() {
    Fixture f; f.start();
    for (int n = 0; n < 3; ++n) {
        f.session.activate(second); require(f.device.paused, "every visit suspends input");
        f.commands.complete("launch"); f.focus(second); f.commands.complete("launch"); f.settle(first);
        require(f.device.plays == 1 && f.device.resumes == n + 1, "automatic return remains active for repeated switches");
    }
}
void navigationCoalesced() {
    Fixture f; f.start(); const int before = f.commands.launches();
    f.session.activate(second); f.session.activate(QString()); f.session.activate(first);
    require(f.commands.launches() == before + 1, "one launch in flight");
    f.commands.complete("launch");
    require(f.commands.launches() == before + 2 && f.commands.requests.last().args.last().contains(first), "latest queued choice wins");
    f.commands.complete("launch"); f.settle(first);
    require(f.device.plays == 1 && f.device.resumes == 1, "coalescing cannot duplicate macro start");
}
void desktopVisit() {
    Fixture f; f.start(); f.session.activate(QString());
    require(f.device.paused && f.commands.requests.last().args.contains("android.intent.category.HOME"), "desktop can be visited during macro");
    f.commands.complete("launch"); f.focus(QString());
    require(f.commands.pending.contains("launch") && f.commands.requests.last().args.last().contains(first), "desktop visit still returns to bound app");
}
void stopVisit() {
    Fixture f; f.start(); f.session.activate(second); f.session.activate(QString());
    f.session.stopMacro(); const int launches = f.commands.launches();
    require(!f.commands.pending.contains("launch"), "stop cancels active and queued navigation/recovery");
    f.settle(second); require(!f.device.playing && !f.session.locked() && f.commands.launches() == launches, "stop never returns or restarts");
}
void pauseVisit() {
    Fixture f; f.start(); f.session.pauseMacro(); f.session.activate(second); f.commands.complete("launch");
    const int launches = f.commands.launches(); f.settle(second);
    require(f.device.paused && f.commands.launches() == launches, "manual pause suppresses automatic return");
    f.session.resumeMacro(); f.commands.complete("launch"); f.settle(first);
    require(f.device.resumes == 1 && f.device.plays == 1, "explicit resume reacquires original app");
}
void pauseRecovery() {
    Fixture f; f.start(); f.focus(second); require(f.commands.pending.contains("launch"), "return pending");
    f.session.pauseMacro(); require(!f.commands.pending.contains("launch"), "pause cancels pending automatic return request");
    const int launches = f.commands.launches(); f.settle(second);
    require(f.device.paused && f.commands.launches() == launches, "paused guard stays quiet");
}
void visitFailure() {
    Fixture f; f.start(); f.session.activate(second);
    f.commands.complete("launch", "Error: visiting app unavailable");
    require(f.session.locked() && f.device.paused, "failed manual navigation keeps macro target");
    f.settle(first); require(f.device.resumes == 1 && f.device.plays == 1, "target recovery works after visit failure");
}
void mappingGuard() {
    Fixture f; f.focus(first); f.session.bindKeymap(first, mapping(20)); f.session.bindKeymap(second, mapping(70)); f.start();
    f.session.activate(second); f.commands.complete("launch"); f.focus(second);
    require(f.device.script == mapping(20), "do not replace a macro-owned mapping while visiting another app");
    f.session.stopMacro(); require(f.device.script == mapping(70), "stop applies current app's own scheme");
}
void fillTabs(Fixture &f) {
    QString list = catalog, names = " - Game    " + first + "\n - R&D Tools    " + second + "\n";
    for (int i = 0; i < 12; ++i) {
        const QString package = "com.extra.app" + QString::number(i);
        list += package + "/.Main\n"; names += " - Application " + QString::number(i) + "    " + package + "\n";
    }
    f.session.refreshApps(); f.commands.complete("catalog", list); f.commands.complete("names", names);
    f.session.bindKeymap(first, QString()); f.session.bindKeymap(second, QString());
    for (int i = 0; i < 12; ++i) require(f.session.bindKeymap("com.extra.app" + QString::number(i), QString()), "remember test tab");
    f.focus(first);
}
void overflowMenu() {
    Fixture f; fillTabs(f); AppBar bar(&f.session); bar.resize(420,66); bar.show(); wait(60);
    auto *more = bar.findChild<QToolButton *>("morePhoneApps"); auto *menu = bar.findChild<QMenu *>("hiddenPhoneApps");
    require(more && more->isVisible() && more->text() == ">" && menu, "explicit more button appears on overflow");
    QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection);
    QSet<QString> entries;
    for (auto *action : menu->actions()) if (!action->data().toString().isEmpty()) {
        require(!entries.contains(action->data().toString()), "unique overflow item per package"); entries.insert(action->data().toString());
        require(!action->text().startsWith("com."), "menu shows labels, package only in tooltip/data");
    }
    require(entries.size() >= 10, "hidden tabs are reachable through menu");
    bar.resize(6000,66); wait(60); require(!more->isVisible(), "more button disappears when all labels fit");
    bar.resize(420,66); wait(60); require(more->isVisible(), "overflow is recalculated on resize");
}
void overflowNavigation() {
    Fixture f; fillTabs(f); AppBar bar(&f.session); bar.resize(420,66); bar.show(); wait(60);
    auto *menu = bar.findChild<QMenu *>("hiddenPhoneApps"); QMetaObject::invokeMethod(menu,"aboutToShow",Qt::DirectConnection);
    QAction *choice = nullptr;
    for (auto *action : menu->actions()) if (action->data().toString() == "com.extra.app10") choice = action;
    require(choice, "hidden target exists"); choice->trigger();
    require(f.commands.pending.contains("launch") && f.commands.requests.last().args.last().contains("com.extra.app10"), "overflow selects the correct application");
}
void overflowCloseAndGuard() {
    Fixture f; fillTabs(f); f.start(); AppBar bar(&f.session); bar.resize(420,66); bar.show(); wait(60);
    auto *tabs = bar.findChild<QTabBar *>("phoneAppTabs"); auto *more = bar.findChild<QToolButton *>("morePhoneApps");
    require(tabs->isEnabled() && more->isEnabled(), "macro guard never disables tab/overflow navigation");
    f.session.closeTab("com.extra.app11"); wait(60);
    auto *menu = bar.findChild<QMenu *>("hiddenPhoneApps"); QMetaObject::invokeMethod(menu,"aboutToShow",Qt::DirectConnection);
    for (auto *action : menu->actions()) require(action->data().toString() != "com.extra.app11", "closed overflow tab is removed");
    QAction *choice = nullptr; for (auto *action : menu->actions()) if (action->data().toString() == "com.extra.app10") choice = action;
    require(choice && choice->isEnabled(), "hidden app is selectable during playback"); choice->trigger();
    require(f.device.paused && f.commands.requests.last().args.last().contains("com.extra.app10"), "menu uses the same pause-before-navigation path");
}
void literalName() {
    Fixture f; fillTabs(f); AppBar bar(&f.session); bar.resize(420,66); bar.show(); wait(50);
    auto *tabs = bar.findChild<QTabBar *>("phoneAppTabs"); bool found = false;
    for (int i=1; i<tabs->count(); ++i) if (tabs->tabData(i).toString()==second) {
        found = true; require(tabs->tabText(i)=="R&&D Tools" && tabs->tabToolTip(i).startsWith("R&D Tools\n"), "ampersand label is displayed literally, not a mnemonic");
    }
    require(found, "named tab found");
}
}
int main(int argc, char **argv) {
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    const QVector<QPair<QString,std::function<void()>>> cases{
        {"name_utf8",nameUtf8},{"name_wrapped",nameWrapped},{"name_diagnostics",nameDiagnostics},{"output_channels",outputChannels},
        {"cache_unbound",cachedWithoutBinding},{"cache_device",cacheDeviceIsolation},{"name_retry",nameRetryCopy},{"name_failure",nameRetryFailure},
        {"ordinary_switch",ordinaryTabSwitch},{"macro_switch",macroTabSwitch},{"repeated_visits",repeatedVisits},{"navigation_queue",navigationCoalesced},
        {"desktop_visit",desktopVisit},{"stop_visit",stopVisit},{"pause_visit",pauseVisit},{"pause_recovery",pauseRecovery},{"visit_failure",visitFailure},
        {"mapping_guard",mappingGuard},{"overflow_menu",overflowMenu},{"overflow_navigation",overflowNavigation},{"overflow_guard",overflowCloseAndGuard},{"literal_name",literalName}};
    int ran=0, failed=0;
    for(const auto &test:cases){
        if(argc>1 && QString::fromLocal8Bit(argv[1])!=test.first)continue;
        ++ran; try {test.second(); qInfo("PASS %s",qPrintable(test.first));}
        catch(const std::exception &e){++failed;qCritical("FAIL %s: %s",qPrintable(test.first),e.what());}
    }
    return ran>0 && failed==0 ? 0 : 1;
}
