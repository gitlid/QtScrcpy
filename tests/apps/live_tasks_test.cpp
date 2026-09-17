#define main original_app_test_main
#include "app_session_test.cpp"
#undef main
#include <QAction>
#include <QMenu>
#include <QMessageBox>
#include <QAbstractButton>
#include "../../QtScrcpy/ui/apprecenttasks.h"
#include "../../QtScrcpy/ui/devicerotationmenu.h"
#include "../../QtScrcpy/QtScrcpyCore/include/viewgeometry.h"

namespace {
QString task(const QString &name, int index = 0, int user = 0, bool legacy = false) {
    return QString("  * Recent #%1: %2{abc #%3 type=standard A=%4 U=%5 sz=1}\n"
        "    userId=%5 effectiveUid=10123\n    realActivity=%4/.Main\n    inRecents=true\n")
        .arg(index).arg(legacy ? "TaskRecord" : "Task").arg(index + 10).arg(name).arg(user);
}
QString snapshot(const QString &tasks, int user = 0) {
    return QString("QSC_USER:%1\nACTIVITY MANAGER RECENT TASKS (dumpsys activity recents)\n%2\nQSC_USER_END:%1\n").arg(user).arg(tasks);
}
QString pendingTag(const FakeCommands &commands, const QString &kind) {
    for (const auto &r : commands.requests) if (r.tag.startsWith("tasks-" + kind + "-") && commands.pending.contains(r.tag)) return r.tag;
    return QString();
}
void reply(FakeCommands &commands, const QString &kind, const QString &output, bool ok = true) {
    const QString tag = pendingTag(commands, kind); require(!tag.isEmpty(), "task command must be pending"); commands.complete(tag, output, ok);
}
void sync(Fixture &f, const QString &tasks) {
    if (pendingTag(f.commands,"snapshot").isEmpty()) wait(1100);
    reply(f.commands,"snapshot",snapshot(tasks));
}
struct TaskFixture {
    FakeCommands commands;
    AppRecentTasks tasks;
    bool allowed = true;
    TaskFixture() : tasks("phone-A", &commands) { tasks.setWriteGuard([this]{ return allowed; }); tasks.start(); }
    void feed(const QString &text) { tasks.refresh(); reply(commands,"snapshot",text); }
};
void parseTasks(bool legacy) {
    QStringList out; int user = -1;
    require(AppRecentTasks::parse(snapshot(task(first,0,0,legacy)+task(second,1,0,legacy)+task(first,2,0,legacy)),&out,&user),"parse AOSP task records");
    require(out == QStringList({first,second}) && user == 0,"deduplicate in MRU order");
    // Task.dump() prints realActivity as mActivityComponent in Android 10+.
    const auto aosp = (task(first,0,0,legacy)+task(second,1,0,legacy))
        .replace("realActivity=", "mActivityComponent=");
    require(AppRecentTasks::parse(snapshot(aosp),&out,&user) && out==QStringList({first,second}),
            "parse actual AOSP mActivityComponent dump field");
    const auto visibleInfo = QString("  Visible recent tasks (most recent first):\n"
        "  * RecentTaskInfo #0: id=99 userId=10 activityType=2 realActivity=com.other.app/.Main\n");
    require(AppRecentTasks::parse(snapshot(aosp+visibleInfo),&out,&user) && out==QStringList({first,second}),
            "appended RecentTaskInfo section cannot contaminate final task");
}
void users() {
    QStringList out; int user = -1;
    require(AppRecentTasks::parse(snapshot(task(first,0,0)+task(second,1,10),10),&out,&user),"parse multi-user dump");
    require(out == QStringList({second}) && user == 10,"only active Android user's apps");
    auto s = snapshot(task(first)); s.replace("QSC_USER_END:0","QSC_USER_END:10");
    require(!AppRecentTasks::parse(s,&out,&user),"reject user change during snapshot");
}
void emptyTasks() {
    QStringList out{first}; int user = -1;
    require(AppRecentTasks::parse(snapshot(QString()),&out,&user) && out.isEmpty(),"confirmed empty list clears visible tasks");
    require(!AppRecentTasks::parse("",&out,&user),"empty response is not an empty task list");
}
void malformed() {
    QStringList out{second}; int user = -1;
    const QStringList bad{"permission denied", snapshot("* Recent #0: VendorTask{no parser}\n"),
        snapshot(task(first)).replace("realActivity=", "unknownField="),
        snapshot(task(first)).replace("QSC_USER_END:0", "truncated"),
        snapshot(task(first)).replace("userId=0", "userId=0xBAD").replace("U=0", "U=bad"),
        snapshot(task(first)).replace("realActivity=com.example.game/.Main", "realActivity=com.bad;reboot/.Main")};
    for (const auto &text:bad) require(!AppRecentTasks::parse(text,&out,&user),"reject incomplete/unsupported/hostile task responses");
    require(out == QStringList({second}),"failed parse is transactional");
}
void homeFiltered() {
    QStringList out; int user = -1;
    const auto home = task("com.example.launcher").replace("type=standard","type=home");
    require(AppRecentTasks::parse(snapshot(home+task(first,1)+task("com.android.systemui",2)),&out,&user),"parse system tasks");
    require(out == QStringList({first}),"home and SystemUI are not closeable application tabs");
    const auto numericHome = task("com.example.launcher",0,0,true)
        .replace("type=standard", "activityType=2").replace("realActivity=", "mActivityComponent=");
    require(AppRecentTasks::parse(snapshot(numericHome+task(first,1)),&out,&user) && out==QStringList({first}),
            "numeric legacy activityType excludes home task");
}
void liveTabs() {
    Fixture f; f.focus(first); require(f.session.bindKeymap(first,mapping(25)),"save original app profile");
    sync(f,task(first)+task(second,1)); require(f.session.tabs()==QStringList({first,second}),"tabs reflect live tasks");
    sync(f,task(second)); require(f.session.tabs()==QStringList({second}),"phone dismissal removes stale tab");
    require(f.session.keymap(first)==mapping(25),"task dismissal does not delete keymaps");
    sync(f,QString()); require(f.session.tabs().isEmpty(),"empty recent list is not replaced by saved history");
}
void taskFailure() {
    TaskFixture f; f.feed(snapshot(task(first)));
    f.tasks.refresh(); reply(f.commands,"snapshot","Permission Denial",false);
    require(f.tasks.packages()==QStringList({first}) && !f.tasks.canClose(first),"failed poll keeps tabs but disables close");
    f.feed(snapshot(task(second))); require(f.tasks.packages()==QStringList({second}) && f.tasks.canClose(second),"successful retry recovers");
}
void boundedPolling() {
    TaskFixture f; const int count=f.commands.requests.size();
    for(int i=0;i<100;++i) f.tasks.refresh();
    require(f.commands.requests.size()==count,"no overlapping dumpsys commands");
    const QString old=pendingTag(f.commands,"snapshot"); f.tasks.stop();
    emit f.commands.finished(old,true,snapshot(task(first)),QString());
    require(!f.tasks.hasSnapshot(),"late response after disconnect cannot update tabs");
}
void closeApp() {
    Fixture f; sync(f,task(first)+task(second,1)); f.start();
    require(f.session.canCloseApp(first),"bound macro app can be closed after explicit UI confirmation");
    f.session.closeApp(first);
    require(!f.device.playing && !f.session.locked() && f.session.closingApp(),"stop macro and auto-recovery before closing");
    const auto args=f.commands.requests.last().args.join(' ');
    require(args.contains("am force-stop --user 0 com.example.game") && args.contains("am get-current-user"),"scoped close with immediate user recheck");
    require(!args.contains("pm clear") && !args.contains("uninstall"),"no data deletion or uninstall");
    const int launches=f.commands.launches(); f.session.activate(first);
    require(f.commands.launches()==launches,"navigation cannot race an in-flight close");
    reply(f.commands,"close","QSC_CLOSE_SENT\n"); reply(f.commands,"snapshot",snapshot(task(second)));
    require(!f.session.closingApp() && !f.session.tabs().contains(first) && !f.session.locked(),"verify close before removing tab; never restart closed macro app");
}
void closeFailure() {
    TaskFixture f; f.feed(snapshot(task(first))); require(f.tasks.closePackage(first),"start close");
    reply(f.commands,"close","Error: Android user changed",false);
    require(!f.tasks.closing() && f.tasks.packages().contains(first),"failed close does not hide app");
}
void closeRetained() {
    TaskFixture f; f.feed(snapshot(task(first))); require(f.tasks.closePackage(first),"start close");
    reply(f.commands,"close","QSC_CLOSE_SENT\n"); reply(f.commands,"snapshot",snapshot(task(first)));
    require(!f.tasks.closing() && f.tasks.packages().contains(first) && f.tasks.status().contains(QString::fromUtf8("仍保留")),"ROM retained task card is not silently removed");
}
void closeGuard() {
    TaskFixture f; f.feed(snapshot(task(first))); f.allowed=false;
    require(!f.tasks.closePackage(first) && pendingTag(f.commands,"close").isEmpty(),"write guard rejects active input owner");
    f.allowed=true; require(!f.tasks.closePackage("com.bad;reboot") && !f.tasks.closePackage(second),"reject unknown or injected package");
}
void closeStale() {
    TaskFixture f; f.feed(snapshot(task(first))); f.tasks.refresh();
    const QString stale=pendingTag(f.commands,"snapshot");
    require(f.tasks.closePackage(first),"close cancels previous read");
    emit f.commands.finished(stale,true,snapshot(task(second)),QString());
    require(f.tasks.packages()==QStringList({first}),"stale completion cannot replace close snapshot");
    reply(f.commands,"close","QSC_CLOSE_SENT\n"); reply(f.commands,"snapshot",snapshot(QString()));
    require(f.tasks.packages().isEmpty(),"fresh post-close snapshot is accepted");
}
void overflowClose(bool accept) {
    Fixture f; QString catalogText, tasks;
    for(int i=0;i<12;++i) { const QString name=QString("com.extra.app%1").arg(i); catalogText+=name+"/.Main\n"; tasks+=task(name,i); }
    f.session.refreshApps(); f.commands.complete("catalog",catalog+catalogText);
    if (f.commands.pending.contains("names")) f.commands.complete("names"," * Game    com.example.game\n");
    sync(f,tasks); AppBar bar(&f.session); bar.resize(420,66); bar.show(); wait(80);
    auto *menu=bar.findChild<QMenu *>("hiddenPhoneApps"); require(menu,"overflow menu");
    QMetaObject::invokeMethod(menu,"aboutToShow",Qt::DirectConnection);
    auto *close=menu->findChild<QMenu *>("closeHiddenPhoneApps"); require(close && !close->actions().isEmpty(),"separate close submenu available");
    QAction *action=close->actions().first(); require(action->isEnabled(),"verified task is closeable");
    QAction *switchAction=nullptr; for(auto *a:menu->actions()) if(a->data()==action->data()) switchAction=a;
    require(switchAction && switchAction->isEnabled(),"direct switching remains available for same app");
    QTimer answer; answer.setInterval(10);
    QObject::connect(&answer,&QTimer::timeout,&bar,[accept]{
        auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if(box && box->button(accept?QMessageBox::Yes:QMessageBox::Cancel)) box->button(accept?QMessageBox::Yes:QMessageBox::Cancel)->click();
    }); answer.start(); action->trigger();
    require(!pendingTag(f.commands,"close").isEmpty()==accept,"only explicit confirmation sends force-stop");
    QMetaObject::invokeMethod(menu,"aboutToShow",Qt::DirectConnection);
    require(menu->findChildren<QMenu *>("closeHiddenPhoneApps").size()==1,"refresh does not leak old submenu objects");
}
void geometry() {
    for(int turn=0;turn<4;++turn) {
        const QSize view(321,643); const QSize raw=qsc::ViewGeometry::sourceSize(view,turn);
        const QList<QPointF> points{QPointF(0,0),QPointF(320,0),QPointF(0,642),QPointF(320,642),QPointF(85.5,300.25)};
        for(const auto &point:points) {
            const auto source=qsc::ViewGeometry::toSource(point,view,turn);
            const auto back=qsc::ViewGeometry::toView(source,view,turn);
            require((back-point).manhattanLength()<0.0001,"pointer forward/inverse roundtrip including all corners");
            require(source.x()>=0 && source.x()<raw.width() && source.y()>=0 && source.y()<raw.height(),"no out-of-bounds Android coordinates");
        }
    }
    const auto topLeft=qsc::ViewGeometry::toSource(QPointF(0,0),QSize(200,100),1);
    require(topLeft==QPointF(0,199),"clockwise view top-left maps to source bottom-left");
}
void wheel() {
    require(qsc::ViewGeometry::deltaToSource(QPoint(0,120),1)==QPoint(120,0),"wheel vector rotated to phone coordinates");
    for(int i=0;i<4;++i) require(qsc::ViewGeometry::deltaToSource(qsc::ViewGeometry::deltaToSource(QPoint(15,-120),i),-i)==QPoint(15,-120),"wheel vector roundtrip");
}
void cursor() {
    for(int i=0;i<4;++i) {
        const QSize view(640,360); const QPoint origin(100,200); const QPointF physical(150,180);
        qsc::ViewMouseEvent event(QEvent::MouseMove,qsc::ViewGeometry::toSource(physical,view,i),origin+physical,
            Qt::NoButton,Qt::NoButton,Qt::NoModifier,origin,view,i);
        const QPointF source(25,40);
        require(event.desktopPosition(source)==origin+qsc::ViewGeometry::toView(source,view,i).toPoint(),"relative mouse-look warp uses actual desktop coordinates");
    }
}
void localMenu() {
    FakeDevice device; DeviceRotationMenu menu(&device,nullptr); int turns=0;
    menu.addViewRotation([&]{return turns;},[&](int next){turns=qsc::ViewGeometry::normalized(next);});
    auto *local=menu.findChild<QMenu *>("viewRotationMenu"); require(local && local->actions().size()==4,"local rotation menu independent of phone commands");
    local->actions()[0]->trigger();require(turns==1,"clockwise");
    local->actions()[1]->trigger();require(turns==0,"counterclockwise");
    local->actions()[2]->trigger();require(turns==2,"half turn");
    local->actions()[3]->trigger();require(turns==0,"reset");
}
}
int main(int argc,char **argv) {
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    const QVector<QPair<QString,std::function<void()>>> tests{
        {"parse_legacy",[]{parseTasks(true);}},{"parse_modern",[]{parseTasks(false);}}, {"users",users}, {"empty",emptyTasks},
        {"malformed",malformed},{"home",homeFiltered},{"sync",liveTabs},{"failure",taskFailure},{"polling",boundedPolling},
        {"close",closeApp},{"close_failure",closeFailure},{"close_retained",closeRetained},{"close_guard",closeGuard},{"close_stale",closeStale},
        {"menu_cancel",[]{overflowClose(false);}},{"menu_close",[]{overflowClose(true);}},
        {"geometry",geometry},{"wheel",wheel},{"cursor",cursor},{"local_menu",localMenu}};
    int ran=0,failed=0;for(const auto &t:tests){if(argc>1&&QString::fromLocal8Bit(argv[1])!=t.first)continue;++ran;
        try{t.second();std::fprintf(stdout,"PASS %s\n",qPrintable(t.first));}
        catch(const std::exception &e){++failed;std::fprintf(stderr,"FAIL %s: %s\n",qPrintable(t.first),e.what());}}
    return ran>0&&failed==0?0:1;
}
