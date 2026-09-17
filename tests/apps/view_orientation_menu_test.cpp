#define main reused_app_fixture_main
#include "app_session_test.cpp"
#undef main
#include <QAction>
#include <QMenu>
#include "../../QtScrcpy/ui/devicerotationmenu.h"

int main(int argc,char **argv) {
    QApplication app(argc,argv);
    try {
        FakeDevice device; FakeCommands commands;
        DeviceRotation rotation(device.serial,nullptr,&commands);
        DeviceRotationMenu menu(&device,nullptr,nullptr,&rotation);
        ViewOrientation policy;policy.setSourceSize(QSize(1080,2340));
        bool accept=true;
        menu.addViewRotation([&]{return policy.rotation();},[&](int turns){if(accept)policy.selectRotation(turns);},
            [&]{return policy.mode();},[&](ViewOrientation::Mode mode){if(accept)policy.setMode(mode);});
        auto *local=menu.findChild<QMenu*>("viewRotationMenu");require(local,"view submenu missing");
        auto *landscape=menu.findChild<QAction*>("viewKeepLandscape");
        auto *portrait=menu.findChild<QAction*>("viewKeepPortrait");
        auto *follow=menu.findChild<QAction*>("viewFollowPhone");
        require(landscape&&portrait&&follow,"mode actions missing");
        QMetaObject::invokeMethod(local,"aboutToShow",Qt::DirectConnection);
        require(follow->isChecked() && !landscape->isChecked(),"menu did not reflect initial state");
        landscape->trigger();require(policy.mode()==ViewOrientation::KeepLandscape && policy.rotation()==1,"landscape action wrong");
        policy.setSourceSize(QSize(2340,1080));
        QMetaObject::invokeMethod(local,"aboutToShow",Qt::DirectConnection);
        require(landscape->isChecked() && policy.rotation()==0,"phone rotation changed selected mode");
        accept=false;portrait->trigger();require(landscape->isChecked()&&!portrait->isChecked(),"cancelled action falsely checked");
        accept=true;menu.findChild<QAction*>("viewRotation3")->trigger();require(policy.mode()==ViewOrientation::FollowPhone,"restore did not cancel lock");
        require(commands.requests.isEmpty(),"desktop mode sent an ADB phone command");
        std::fprintf(stdout,"PASS orientation menu, cancellation, state refresh and zero phone commands\n");return 0;
    }catch(const std::exception &e){std::fprintf(stderr,"FAIL orientation menu: %s\n",e.what());return 1;}
}
