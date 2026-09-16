#include <QApplication>
#include <QAbstractButton>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>
#include <stdexcept>
#include "../../QtScrcpy/ui/devicerotation.h"
#include "../../QtScrcpy/ui/devicerotationmenu.h"

namespace {
void require(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
void wait(int ms) { QEventLoop loop; QTimer::singleShot(ms,&loop,&QEventLoop::quit); loop.exec(); }
RotationState original(bool tablet=false) {
    RotationState s; s.current=0;s.portrait=tablet?1:0;s.landscape=tablet?0:1;s.userRotation=0;s.userMode=0;
    s.fixedPolicy="default";s.fixedExact=true;s.fixedEffective=false;s.accelerometer="1";s.rotationSetting="0";return s;
}
QString dump(const RotationState &s,bool symbolic=false) {
    return QString("WINDOW MANAGER DISPLAY CONTENTS\n  Display: mDisplayId=0\n    DisplayRotation\n"
        "      mRotation=%1 mDeferredRotationPauseCount=0\n      mLandscapeRotation=ROTATION_%2 mSeascapeRotation=ROTATION_270\n"
        "      mPortraitRotation=ROTATION_%3 mUpsideDownRotation=ROTATION_180\n"
        "      mUserRotationMode=%4 mUserRotation=ROTATION_%5 mAllowAllRotations=unknown\n      mFixedToUserRotation=%6\n")
        .arg(symbolic?QString("ROTATION_%1").arg(s.current*90):QString::number(s.current))
        .arg(s.landscape*90).arg(s.portrait*90).arg(s.userMode?"USER_ROTATION_LOCKED":"USER_ROTATION_FREE")
        .arg(s.userRotation*90).arg(s.fixedEffective?"true":"false");
}
class Commands : public AppCommands {
public:
    RotationState live=original();
    QString identity="physical-device", user="0";
    bool modern=true, supported=true, rejectRotation=false, disconnected=false, permission=false;
    int mutations=0, rejectAt=-1;
    QStringList pending;
    QList<QStringList> history;
    std::function<void(const QStringList&)> hook;
    void cancelAll() override { pending.clear(); }
    void cancel(const QString &tag) override { pending.removeAll(tag); }
    void run(const QString &tag,const QString &,const QStringList &args,int) override {
        pending.append(tag);history.append(args);
        QTimer::singleShot(0,this,[this,tag,args]{
            if(!pending.removeAll(tag))return;
            if(hook)hook(args);
            if(disconnected){emit finished(tag,false,QString(),"device offline");return;}
            const QString cmd=args.join(' ');
            QString out,err; bool ok=true;
            if(cmd=="shell getprop ro.serialno")out=identity+"\n";
            else if(cmd=="shell am get-current-user")out=user+"\n";
            else if(cmd=="shell wm help")out=!supported?"Window manager help\n":modern?
                "  user-rotation [-d DISPLAY_ID] [free|lock] [rotation]\n  fixed-to-user-rotation [enabled|disabled|default]\n":
                "  set-user-rotation [free|lock] [-d DISPLAY_ID] [rotation]\n  set-fix-to-user-rotation [enabled|disabled]\n";
            else if(cmd=="shell dumpsys window displays")out=dump(live,modern);
            else if(cmd=="shell wm fixed-to-user-rotation")out=live.fixedPolicy+"\n";
            else if(args.size()==7 && args[1]=="settings" && args[4]=="get")
                out=(args[6]=="accelerometer_rotation"?live.accelerometer:live.rotationSetting)+"\n";
            else {
                ++mutations;
                if(permission||mutations==rejectAt) { ok=false;err="SecurityException: rejected for test"; }
                else if(args.size()>=4 && args[1]=="wm") {
                    if(args[2].contains("user-rotation")&&!args[2].contains("fixed")&&!args[2].contains("fix-to")) {
                        if(args[3]=="lock") { live.userMode=1;live.userRotation=args[4].toInt();live.rotationSetting=args[4];live.accelerometer="0";
                            if(!rejectRotation)live.current=live.userRotation; }
                        else { live.userMode=0;live.accelerometer="1"; }
                    } else { live.fixedPolicy=args[3];live.fixedEffective=args[3]=="enabled";if(!rejectRotation&&live.fixedEffective)live.current=live.userRotation; }
                } else if(args.size()>=7 && args[1]=="settings") {
                    const QString val=args[4]=="delete"?"null":args[7];
                    if(args[6]=="accelerometer_rotation"){live.accelerometer=val;live.userMode=val=="0"?1:0;}
                    else {live.rotationSetting=val;live.userRotation=val=="null"?0:val.toInt();}
                } else {ok=false;err="unrecognized test command: "+cmd;}
            }
            emit finished(tag,ok,out,err);
        });
    }
};
struct Fixture {
    QTemporaryDir directory;
    Commands commands;
    DeviceRotation rotation;
    bool done=false, success=false, accept=true;
    QString message, confirmation;
    Fixture(const QString &path=QString(),const QString &serial="wifi:5555")
        : rotation(serial,nullptr,&commands,path.isEmpty()?directory.path():path) {
        QObject::connect(&rotation,&DeviceRotation::confirmationRequired,&rotation,[this](const QString &text){
            confirmation=text;rotation.confirm(accept);
        });
        QObject::connect(&rotation,&DeviceRotation::finished,&rotation,[this](bool ok,const QString &text){done=true;success=ok;message=text;});
    }
    void run(DeviceRotation::Mode mode) {
        done=false;rotation.request(mode);
        for(int i=0;i<600 && !done;++i)wait(10);
        require(done,"rotation operation must finish within 6 seconds");
    }
    void record() {run(DeviceRotation::Landscape);require(success,"initial landscape request succeeds");}
};
void parsers(bool symbolic) {
    auto s=original();s.userMode=1;s.userRotation=3;s.current=3;s.fixedEffective=true;
    RotationState parsed;require(RotationState::parseDisplay(dump(s,symbolic),&parsed),"parse stock display rotation");
    require(parsed.userMode==1&&parsed.current==3&&parsed.userRotation==3&&parsed.fixedEffective,"read exact primary values");
}
void secondary() {
    QString wrong=dump(original(true));wrong.replace("mDisplayId=0","mDisplayId=2");
    RotationState parsed;require(!RotationState::parseDisplay(wrong,&parsed),"secondary display alone cannot be used");
    require(RotationState::parseDisplay(wrong+dump(original())+wrong,&parsed)&&parsed.portrait==0,"choose only primary block");
}
void incomplete() {
    RotationState s;
    QString broken=dump(original());broken.replace("mFixedToUserRotation=false","");
    require(!RotationState::parseDisplay(broken,&s),"missing original fixed state is not guessed");
    broken=dump(original());broken.replace("mPortraitRotation=ROTATION_0","mPortraitRotation=ROTATION_90");
    require(!RotationState::parseDisplay(broken,&s),"portrait and landscape axes must differ");
}
void schema() {
    auto obj=original().json();RotationState state;
    require(RotationState::fromJson(obj,&state),"valid backup schema");
    obj["fixedPolicy"]="enabled;reboot";require(!RotationState::fromJson(obj,&state),"policy injection rejected");
    obj=original().json();obj["userRotation"]=1.5;require(!RotationState::fromJson(obj,&state),"fractional angle rejected");
    obj=original().json();obj["accelerometer"]="0";require(!RotationState::fromJson(obj,&state),"inconsistent mode rejected");
}
void landscape() {
    Fixture f;f.record();require(f.commands.live.current==1&&f.commands.live.userMode==1&&f.commands.live.fixedEffective,"landscape fixed on device");
    require(QFile::exists(f.rotation.backupPath()),"durable backup before settings write");
}
void tablet() {Fixture f;f.commands.live=original(true);f.record();require(f.commands.live.current==0,"landscape uses natural tablet orientation, not hardcoded 90");}
void toggle(bool locked) {
    Fixture f;if(locked){f.commands.live.userMode=1;f.commands.live.accelerometer="0";}
    f.run(DeviceRotation::Toggle);require(f.success&&f.commands.live.current==1,"toggle reaches opposite axis");
    require(f.commands.live.userMode==(locked?1:0)&&f.commands.live.fixedPolicy=="default","toggle preserves sensor mode and app policy");
}
void restore(bool legacy=false) {
    Fixture f;f.commands.modern=!legacy;const auto originalState=f.commands.live;f.record();
    f.run(DeviceRotation::Restore);require(f.success,"restore succeeds");
    require(f.commands.live.accelerometer==originalState.accelerometer&&f.commands.live.userRotation==originalState.userRotation,"original mode restored");
    require(f.commands.live.fixedPolicy==(legacy?"disabled":"default"),"exact modern policy or explicit legacy effective policy");
    require(!QFile::exists(f.rotation.backupPath()),"verified restore removes recovery record");
    if(legacy)require(f.confirmation.contains(QString::fromUtf8("无法区分")),"legacy restore limitation is disclosed BEFORE write");
}
void repeated() {
    Fixture f;f.record();QFile b(f.rotation.backupPath());require(b.open(QIODevice::ReadOnly),"read backup");const auto bytes=b.readAll();b.close();
    f.run(DeviceRotation::Portrait);require(f.success&&f.commands.live.current==0,"second setting");
    b.open(QIODevice::ReadOnly);require(bytes==b.readAll(),"never replace first original state");b.close();
    f.run(DeviceRotation::Restore);require(f.success&&f.commands.live.userMode==0,"restores before first setting");
}
void cancel() {Fixture f;f.accept=false;f.run(DeviceRotation::Landscape);require(!f.success&&f.commands.mutations==0&&!QFile::exists(f.rotation.backupPath()),"cancel does not write phone or backup");}
void unavailable() {Fixture f;f.commands.supported=false;f.run(DeviceRotation::Landscape);require(!f.success&&f.commands.mutations==0,"unsupported ROM makes no changes");}
void identity() {Fixture f;f.commands.identity="unknown";f.run(DeviceRotation::Portrait);require(!f.success&&f.commands.mutations==0,"cannot recover wrong phone on reused wifi address");}
void corrupt() {Fixture f;f.record();QFile b(f.rotation.backupPath());b.open(QIODevice::WriteOnly);b.write("corrupt");b.close();int n=f.commands.mutations;
    f.run(DeviceRotation::Restore);require(!f.success&&f.commands.mutations==n,"corrupt backup cannot trigger writes");b.open(QIODevice::ReadOnly);require(b.readAll()=="corrupt","preserve corrupt record");}
void isolations(bool androidUser) {
    QTemporaryDir shared;Fixture f(shared.path());f.record();
    Fixture g(shared.path());if(androidUser)g.commands.user="10";else g.commands.identity="another-phone";
    g.run(DeviceRotation::Restore);require(!g.success&&g.commands.mutations==0,"backup cannot cross devices/users");
}
void reconnect() {
    QTemporaryDir shared;QString path;
    {Fixture f(shared.path());f.record();path=f.rotation.backupPath();}
    Fixture g(shared.path(),"USB-device");g.commands.live.userMode=1;g.commands.live.userRotation=1;g.commands.live.rotationSetting="1";g.commands.live.accelerometer="0";
    g.commands.live.fixedEffective=true;g.commands.live.fixedPolicy="enabled";g.run(DeviceRotation::Restore);
    require(g.success&&!QFile::exists(path),"same physical phone restores after transport switch/restart");
}
void rollback() {
    Fixture f;const auto s=f.commands.live;f.commands.rejectAt=2;f.run(DeviceRotation::Landscape);
    require(!f.success&&f.commands.live.sameSettings(s)&&QFile::exists(f.rotation.backupPath()),"partial failure rolls back but retains original backup");
}
void permission() {Fixture f;f.commands.permission=true;f.run(DeviceRotation::Landscape);require(!f.success&&QFile::exists(f.rotation.backupPath()),"permission denied cannot be shown as success");}
void userChange() {
    Fixture f;int userReads=0;
    f.commands.hook=[&](const QStringList&a){if(a.join(' ')=="shell am get-current-user"&&++userReads>1)f.commands.user="10";};
    f.run(DeviceRotation::Landscape);require(!f.success&&f.commands.mutations==0,"recheck Android user before each mutation");
}
void guard() {Fixture f;f.rotation.setWriteGuard([]{return false;});f.run(DeviceRotation::Landscape);require(!f.success&&f.commands.mutations==0,"new macro cannot race rotation writes");}
void disconnect() {Fixture f;f.commands.hook=[&](const QStringList&a){if(a.join(' ').startsWith("shell wm set"))return;
    if(a.join(' ')=="shell wm fixed-to-user-rotation enabled") {f.rotation.disconnectDevice();f.commands.disconnected=true;}};
    f.run(DeviceRotation::Landscape);require(!f.success&&QFile::exists(f.rotation.backupPath()),"disconnect retains recoverable snapshot");}
void ignored() {Fixture f;f.commands.rejectRotation=true;f.run(DeviceRotation::Landscape);require(!f.success&&f.commands.live.userMode==0,"ROM ignoring rotation is detected and rolled back");}
void noBackup() {Fixture f;f.run(DeviceRotation::Restore);require(!f.success&&f.commands.mutations==0,"no backup must not mean default portrait");}
void concurrent() {
    QTemporaryDir shared;Fixture f(shared.path());QObject::disconnect(&f.rotation,SIGNAL(confirmationRequired(QString)),nullptr,nullptr);
    bool waiting=false;QObject::connect(&f.rotation,&DeviceRotation::confirmationRequired,&f.rotation,[&](const QString&){waiting=true;});
    f.rotation.request(DeviceRotation::Landscape);for(int i=0;i<100&&!waiting;++i)wait(10);require(waiting,"first operation awaiting confirmation");
    Fixture g(shared.path(),"USB-device");g.run(DeviceRotation::Portrait);require(!g.success&&g.commands.mutations==0,"physical device lock serializes both connections");f.rotation.confirm(false);
}
void nullSettings() {Fixture f;f.commands.live.rotationSetting="null";f.record();f.run(DeviceRotation::Restore);require(f.success&&f.commands.live.rotationSetting=="null","restore absent setting via delete rather than inventing value");}
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
void menuActions() {
    FakeDevice device;Commands commands;QTemporaryDir dir;DeviceRotation rotation(device.serial,nullptr,&commands,dir.path());
    DeviceRotationMenu menu(&device,nullptr,nullptr,&rotation);QStringList labels;
    for(auto *a:menu.actions())if(!a->isSeparator())labels<<a->text();
    const QStringList expected{QString::fromUtf8("切换横竖屏"),QString::fromUtf8("固定横屏"),QString::fromUtf8("固定竖屏"),QString::fromUtf8("恢复原设置")};
    require(labels.size()==expected.size(),"four menu options");
    for(int i=0;i<expected.size();++i)require(labels.at(i)==expected.at(i),"menu option order and label");
    if(qEnvironmentVariableIsSet("QSC_ROTATION_SCREENSHOT")) {menu.popup(QPoint(80,80));wait(50);require(menu.grab().save(qEnvironmentVariable("QSC_ROTATION_SCREENSHOT")),"capture actual Qt menu");menu.close();}
}
void menuGuard(bool stop) {
    FakeDevice device;device.playActionMacro(1,0);Commands commands;QTemporaryDir dir;
    DeviceRotation rotation(device.serial,nullptr,&commands,dir.path());DeviceRotationMenu menu(&device,nullptr,nullptr,&rotation);
    QTimer answer;answer.setInterval(10);
    QObject::connect(&answer,&QTimer::timeout,&menu,[&]{
        auto *box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if(box) { const auto choice=box->windowTitle()==QString::fromUtf8("旋转前停止预制操作")&&stop?QMessageBox::Yes:QMessageBox::Cancel;
            auto *button=box->button(choice);if(!button)button=box->button(QMessageBox::Ok);if(button)button->click(); }
    });answer.start();menu.choose(DeviceRotation::Landscape);
    for(int i=0;i<200&&rotation.busy();++i)wait(10);
    require(device.playing==!stop,"stop/cancel chosen explicitly, not silent bypass");require(commands.mutations==0,"confirmation cancel writes nothing");
}
}
int main(int argc,char **argv) {
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    const QVector<QPair<QString,std::function<void()>>> tests{
        {"parse_android11",[]{parsers(false);}},{"parse_android16",[]{parsers(true);}},{"parse_secondary",secondary},{"parse_incomplete",incomplete},{"backup_schema",schema},
        {"modern_landscape",landscape},{"natural_landscape",tablet},{"toggle_auto",[]{toggle(false);}},{"toggle_locked",[]{toggle(true);}},
        {"legacy_restore",[]{restore(true);}},{"restore_exact",[]{restore(false);}},{"restore_original",repeated},{"cancel_no_write",cancel},
        {"unsupported",unavailable},{"identity_missing",identity},{"backup_corrupt",corrupt},{"device_isolation",[]{isolations(false);}},
        {"user_isolation",[]{isolations(true);}},{"reconnect_restore",reconnect},{"rollback",rollback},{"permission",permission},{"user_changed",userChange},
        {"write_guard",guard},{"disconnect",disconnect},{"rom_ignores",ignored},{"no_backup",noBackup},{"concurrent",concurrent},
        {"null_setting",nullSettings},{"menu_actions",menuActions},{"menu_cancel_macro",[]{menuGuard(false);}},{"menu_stop_macro",[]{menuGuard(true);}}};
    int count=0,failed=0;
    for(const auto &t:tests){if(argc>1&&QString::fromLocal8Bit(argv[1])!=t.first)continue;++count;
        try{t.second();std::fprintf(stdout,"PASS %s\n",qPrintable(t.first));}
        catch(const std::exception&e){++failed;std::fprintf(stderr,"FAIL %s: %s\n",qPrintable(t.first),e.what());}}
    return count>0&&failed==0?0:1;
}
