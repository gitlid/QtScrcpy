#define main app_session_tests_main
#include "app_session_test.cpp"
#undef main
#include "../../QtScrcpy/ui/systempanelaction.h"
namespace {
const QString overlay="  mCurrentFocus=Window{abc u0 UpSlideTransparentView}\n";
const QString foreground="  mCurrentFocus=Window{abc u0 com.example.game/.Main}\n";
struct PanelFixture {
    FakeDevice device; FakeCommands commands; SystemPanelAction panel;
    PanelFixture():panel(&device,nullptr,&commands){}
    void reply(const QString &text,bool ok=true){require(!commands.requests.isEmpty(),"focus query exists");commands.complete(commands.requests.last().tag,text,ok);}
};
void normal(){PanelFixture f;f.panel.request(false);f.reply(foreground);require(f.device.panelRequests==QStringList{"notifications"}&&f.device.backCount==0,"normal application is not navigated back");}
void vivo(){PanelFixture f;f.panel.request(true);f.reply(overlay);require(f.device.backCount==1&&f.device.panelRequests.isEmpty(),"vivo overlay dismissed before swiping");wait(240);f.reply(foreground);require(f.device.panelRequests.isEmpty(),"wait for dismissal animation");wait(540);f.reply(foreground);require(f.device.panelRequests==QStringList{"settings"},"right-side swipe after collapse confirmed");}
void notification(){PanelFixture f;f.panel.request(true);f.reply("mCurrentFocus=Window{abc u0 NotificationShade}");require(f.device.backCount==1,"notification overlay dismissed before right-side swipe");wait(240);f.reply(foreground);require(f.device.panelRequests.isEmpty(),"wait for dismissal animation");wait(540);f.reply(foreground);require(f.device.panelRequests==QStringList{"settings"},"notification-to-settings transition");}
void retained(){PanelFixture f;f.panel.request(true);for(int i=0;i<6;++i){f.reply(overlay);wait(240);}require(f.device.backCount==1&&f.device.panelRequests.isEmpty(),"do not repeat BACK or swipe a retained overlay");}
void cancel(){PanelFixture f;f.panel.request(true);f.reply(overlay);f.panel.cancel();const int size=f.commands.requests.size();wait(300);require(f.commands.requests.size()==size&&f.device.panelRequests.isEmpty(),"stop invalidates pending collapse verification");}
void stale(){PanelFixture f;f.panel.request(false);const QString tag=f.commands.requests.last().tag;f.panel.request(true);emit f.commands.finished(tag,true,foreground,QString());require(f.device.panelRequests.isEmpty(),"stale query cannot dispatch an old request");f.reply(foreground);require(f.device.panelRequests==QStringList{"settings"},"latest request wins");}
void failure(){PanelFixture f;f.panel.request(true);f.reply(QString(),false);require(f.device.backCount==0&&f.device.panelRequests.isEmpty(),"unknown focus never sends BACK");}
void busy(){PanelFixture f;f.panel.request(true);f.device.playActionMacro(1,0);require(f.commands.pending.isEmpty(),"new macro cancels pending UI action");f.panel.request(true);require(f.device.panelRequests.isEmpty(),"blocked while macro owns input");}
void gone(){PanelFixture f;f.panel.request(false);f.device.disconnectDevice();require(f.commands.pending.isEmpty(),"disconnect cancels query");f.panel.request(true);require(f.commands.pending.isEmpty(),"no replacement-session actions");}
}
int main(int argc,char **argv){
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    const QVector<QPair<QString,std::function<void()>>> cases{{"normal",normal},{"vivo",vivo},{"notification",notification},{"retained",retained},{"cancel",cancel},{"stale",stale},{"failure",failure},{"busy",busy},{"disconnect",gone}};
    for(const auto &test:cases)if(argc==2&&QString(argv[1])==test.first){try{test.second();std::printf("PASS %s\n",qPrintable(test.first));return 0;}catch(const std::exception &e){std::fprintf(stderr,"FAIL %s: %s\n",qPrintable(test.first),e.what());return 1;}}
    return 2;
}
