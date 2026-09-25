#include <QApplication>
#include <QEventLoop>
#include <QTimer>
#include <QLayout>
#include <QImage>
#include <QColor>
#include <QScrollArea>
#include <QScrollBar>
#include <QPushButton>
#include <QWheelEvent>
#include <QPointer>
#include "../QtScrcpy/ui/toolform.h"
#include "../QtScrcpy/ui/tooldock.h"
#include <cstdio>
#include <stdexcept>
#include "../QtScrcpy/ui/videoform.h"
#include "../QtScrcpy/render/qyuvopenglwidget.h"
#include "../QtScrcpy/uibase/keepratiowidget.h"

// Only the phone-authorization prompt is bypassed. These tests use the same
// VideoForm, renderer, layout and session callback as the shipping application.
struct ViewOrientationTestAccess {
    static void mode(VideoForm &v,ViewOrientation::Mode mode) { auto p=v.m_viewOrientation;p.setMode(mode);v.applyViewOrientation(p); }
    static void turn(VideoForm &v,int delta) { auto p=v.m_viewOrientation;p.selectRotation(v.viewRotation()+delta);v.applyViewOrientation(p); }
    static void session(VideoForm &v,const QSize &size) { v.onVideoSessionChanged(size,false); }
    static QYUVOpenGLWidget *surface(VideoForm &v) { return v.m_videoWidget.data(); }
    static void tools(VideoForm &v,bool shown) { v.showToolForm(shown); }
};
namespace {
const QSize portrait(1080,2340), landscape(2340,1080);
void require(bool ok,const char *why) { if(!ok)throw std::runtime_error(why); }
void wait(int ms=50) { QEventLoop e;QTimer::singleShot(ms,&e,&QEventLoop::quit);e.exec(); }
void setup(VideoForm &v) { v.show();wait();v.updateShowSize(portrait);ViewOrientationTestAccess::turn(v,1);v.setGeometry(90,100,820,420);wait(); }
void cycle(VideoForm &v,const QRect &bounds) {
    for(int i=0;i<12;++i) {
        const QSize raw=i%2?portrait:landscape;
        ViewOrientationTestAccess::session(v,raw);wait(10);
        require(v.geometry()==bounds,"phone session moved/resized the locked window");
        require(v.frameSize()==raw,"raw controller frame was replaced by display geometry");
        require(v.viewRotation()==(i%2?1:0),"native game acquired a second rotation");
        require(ViewOrientationTestAccess::surface(v)->viewRotation()==v.viewRotation(),"texture/input angles differ");
    }
}
void sequence() { VideoForm v(false,false,false);setup(v);cycle(v,v.geometry()); }
void skinned() { VideoForm v(false,true,false);setup(v);v.setGeometry(80,90,1100,650);wait();cycle(v,v.geometry()); }
void frameless() { VideoForm v(true,false,false);setup(v);cycle(v,v.geometry()); }
void preserveSelection() { VideoForm v(false,false,false);setup(v);const QRect r=v.geometry();ViewOrientationTestAccess::mode(v,ViewOrientation::KeepLandscape);wait();require(v.geometry()==r,"selecting current axis reset size");cycle(v,r); }
void bounds() { VideoForm v(false,false,false);setup(v);v.resize(900,240);wait();const QRect r=v.geometry();for(const QSize &raw:{QSize(2000,1080),QSize(1080,2400),QSize(1600,900)}){v.updateShowSize(raw);wait();require(v.geometry()==r,"aspect change grew window");auto *s=ViewOrientationTestAccess::surface(v);require(s->parentWidget()->rect().contains(s->geometry()),"view overflowed available width/height");} }
void fullscreen() { VideoForm v(false,false,false);setup(v);const QRect r=v.geometry();v.switchFullScreen();wait(150);require(v.isFullScreen(),"fullscreen not entered");cycle(v,v.geometry());require(v.isFullScreen(),"source exited fullscreen");v.switchFullScreen();wait();require(v.geometry()==r,"fullscreen exit lost chosen geometry"); }
void maximized() { VideoForm v(false,false,false);setup(v);v.showMaximized();wait(150);require(v.isMaximized(),"maximize not entered");cycle(v,v.geometry());require(v.isMaximized(),"source unmaximized window"); }
void follow() { VideoForm v(false,false,false);setup(v);ViewOrientationTestAccess::mode(v,ViewOrientation::FollowPhone);wait();require(v.viewRotation()==0 && v.width()<v.height(),"cancel did not follow portrait");v.updateShowSize(landscape);wait();require(v.viewRotation()==0 && v.width()>v.height(),"follow phone landscape lost"); }
void docked() {
    VideoForm v(false,false,true);setup(v);
    auto *tools=v.findChild<ToolForm*>();auto *dock=v.findChild<QScrollArea*>("integratedToolDock");
    require(tools&&dock&&!tools->isWindow()&&tools->window()==&v,"tools must share the video native window");
    require(v.rect().contains(QRect(dock->mapTo(&v,QPoint()),dock->size())),"dock stays inside video client area");
    const auto *surface=ViewOrientationTestAccess::surface(v);
    require(surface->mapTo(&v,QPoint(surface->width(),0)).x()<=dock->mapTo(&v,QPoint()).x(),"tools cannot cover phone pixels");
    const QPoint position=dock->mapTo(&v,QPoint());v.move(160,170);wait();
    require(dock->mapTo(&v,QPoint())==position,"move keeps dock relative position");
    for(QWidget *w:QApplication::topLevelWidgets())require(w!=tools&&w!=dock,"no detached tool window");
    require(tools->findChild<QPushButton*>("expandNotifyBtn")->toolTip().contains(QString::fromUtf8("左上")),"notification tooltip");
    require(tools->findChild<QPushButton*>("expandSettingsBtn")->toolTip().contains(QString::fromUtf8("右上")),"settings tooltip");
    for(auto *b:tools->findChildren<QPushButton*>())require(b->focusPolicy()==Qt::NoFocus,"toolbar must not steal phone keyboard focus");
    if(!qEnvironmentVariable("QSC_DOCK_SCREENSHOT").isEmpty())v.grab().save(qEnvironmentVariable("QSC_DOCK_SCREENSHOT"));
}
void dockScroll() {
    VideoForm v(false,false,true);setup(v);v.resize(650,280);wait();
    auto *dock=v.findChild<QScrollArea*>("integratedToolDock");auto *tools=v.findChild<ToolForm*>();
    require(dock->verticalScrollBar()->maximum()>0,"short windows scroll tool buttons");
    dock->ensureWidgetVisible(tools->findChild<QPushButton*>("clipboardBtn"));wait();
    auto *last=tools->findChild<QPushButton*>("clipboardBtn");
    require(dock->viewport()->rect().contains(QRect(last->mapTo(dock->viewport(),QPoint()),last->size())),"last tool button is reachable");
    const QRect before=v.geometry();
    QWheelEvent wheel(QPointF(10,10),QPointF(10,10),QPoint(),QPoint(0,-120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
    QApplication::sendEvent(dock->viewport(),&wheel);require(wheel.isAccepted()&&v.geometry()==before,"dock wheel is consumed even at scroll limit");
}
void dockFullscreen() {
    VideoForm v(false,false,true);setup(v);const QRect before=v.geometry();
    v.switchFullScreen();wait(120);auto *dock=v.findChild<QScrollArea*>("integratedToolDock");
    require(v.isFullScreen()&&dock->isVisible()&&!dock->isWindow(),"fullscreen tools remain integrated and usable");
    cycle(v,v.geometry());v.switchFullScreen();wait();require(v.geometry()==before,"dock fullscreen restore retains geometry");
}
void dockHidden() {
    VideoForm v(false,false,false);setup(v);auto *dock=v.findChild<QScrollArea*>("integratedToolDock");
    require(dock->isHidden(),"show-toolbar preference respected");
    const QRect before=v.geometry();ViewOrientationTestAccess::tools(v,true);wait();
    require(dock->isVisible()&&v.geometry()==before,"show dock without resizing user window");
    ViewOrientationTestAccess::tools(v,false);wait();require(dock->isHidden()&&v.geometry()==before,"hide dock returns space to video");
}
void dockSkin() {
    VideoForm v(true,true,true);setup(v);v.resize(1000,620);wait();cycle(v,v.geometry());
    auto *dock=v.findChild<QScrollArea*>("integratedToolDock");
    require(v.rect().contains(QRect(dock->mapTo(&v,QPoint()),dock->size())),"skinned dock remains within window after source rotation");
}
void dockLifetime() {
    auto *v=new VideoForm(false,false,true);v->show();wait();QPointer<ToolForm> tools=v->findChild<ToolForm*>();
    QPointer<QScrollArea> dock=v->findChild<QScrollArea*>("integratedToolDock");require(tools&&dock,"owned tools created");
    delete v;require(!tools&&!dock,"closing video destroys all toolbar widgets");
}
void render() {
    VideoForm v(false,false,false);v.show();auto *s=ViewOrientationTestAccess::surface(v);s->show();wait(120);
    require(s->isValid(),"no real OpenGL context");v.updateShowSize(QSize(32,64));ViewOrientationTestAccess::turn(v,1);v.setGeometry(100,100,480,240);wait();const QRect r=v.geometry();
    for(int i=0;i<4;++i) {
        const int w=i%2?64:32,h=i%2?32:64;
        QByteArray y(w*h,0),u(w*h/4,0),z(w*h/4,0);
        const int ys[4]={63,173,32,235},us[4]={102,42,240,128},vs[4]={240,26,118,128};
        for(int row=0;row<h;++row)for(int col=0;col<w;++col)y[row*w+col]=char(ys[(row>=h/2?2:0)+(col>=w/2?1:0)]);
        for(int row=0;row<h/2;++row)for(int col=0;col<w/2;++col){const int q=(row>=h/4?2:0)+(col>=w/4?1:0);u[row*w/2+col]=char(us[q]);z[row*w/2+col]=char(vs[q]);}
        ViewOrientationTestAccess::session(v,QSize(w,h));
        v.updateRender(w,h,reinterpret_cast<quint8*>(y.data()),reinterpret_cast<quint8*>(u.data()),reinterpret_cast<quint8*>(z.data()),w,w/2,w/2);wait(100);
        require(v.geometry()==r,"render path changed locked geometry");require(s->frameSize()==QSize(w,h),"GPU decoder size changed");
        const QImage image=s->grabFramebuffer();require(!image.isNull(),"empty rendered image");
        // Raw quadrants are red/green/blue/white. Upright landscape has red TL;
        // clockwise portrait must have blue TL. This expectation is not
        // calculated with the production transform.
        const QColor tl=image.pixelColor(image.width()/4,image.height()/4);
        require(i%2?(tl.red()>150 && tl.blue()<80):(tl.blue()>150 && tl.red()<80),"texture rotation was stacked on native landscape");
        if(!qEnvironmentVariable("QSC_LOCKED_VIEW_SCREENSHOT").isEmpty())image.save(qEnvironmentVariable("QSC_LOCKED_VIEW_SCREENSHOT")+QString::number(i)+".png");
    }
}
}
int main(int argc,char **argv) {
    QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    const struct {const char *name;void(*run)();} cases[]={{"sequence",sequence},{"skinned",skinned},{"frameless",frameless},{"preserve_selection",preserveSelection},{"bounds",bounds},{"fullscreen",fullscreen},{"maximized",maximized},{"follow",follow},{"render",render},
        {"docked",docked},{"dock_scroll",dockScroll},{"dock_fullscreen",dockFullscreen},{"dock_hidden",dockHidden},{"dock_skin",dockSkin},{"dock_lifetime",dockLifetime}};
    for(const auto &test:cases)if(argc==2 && QString::fromUtf8(argv[1])==test.name){try{test.run();std::printf("PASS VideoForm %s\n",test.name);return 0;}catch(const std::exception &e){std::fprintf(stderr,"FAIL %s: %s\n",test.name,e.what());return 1;}}
    return 2;
}
