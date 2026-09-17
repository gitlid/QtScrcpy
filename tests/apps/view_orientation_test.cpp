#include <QCoreApplication>
#include <QPointF>
#include <QString>
#include <cstdio>
#include <stdexcept>
#include "../../QtScrcpy/ui/vieworientation.h"
#include "../../QtScrcpy/QtScrcpyCore/include/viewgeometry.h"

namespace {
const QSize portrait(1080,2340), landscape(2340,1080);
void require(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
ViewOrientation at(const QSize &size) { ViewOrientation p; p.setSourceSize(size); return p; }
void sequence(ViewOrientation &p, int portraitTurns, int landscapeTurns) {
    for (int i=0; i<100; ++i) {
        const bool horizontal=i%2;
        const QSize raw=horizontal?landscape:portrait;
        p.setSourceSize(raw);
        require(p.rotation()==(horizontal?landscapeTurns:portraitTurns),"wrong compensated turn / accumulated rotation");
        require(p.sourceSize()==raw,"source/controller geometry was rotated");
    }
}
void follow() { auto p=at(portrait); require(p.mode()==ViewOrientation::FollowPhone,"default changed"); sequence(p,0,0); }
void clockwise() { auto p=at(portrait);p.selectRotation(1); require(p.mode()==ViewOrientation::KeepLandscape,"CW must keep landscape"); sequence(p,1,0); }
void counterclockwise() { auto p=at(portrait);p.selectRotation(-1); sequence(p,3,0); }
void nativeLandscape() { auto p=at(landscape);p.setMode(ViewOrientation::KeepLandscape);sequence(p,1,0); }
void keepPortrait() { auto p=at(landscape);p.setMode(ViewOrientation::KeepPortrait);sequence(p,0,1); }
void halfTurn() { auto p=at(portrait);p.selectRotation(1);p.selectRotation(3);sequence(p,3,2); }
void reset() { auto p=at(portrait);p.selectRotation(1);p.setSourceSize(landscape);p.setMode(ViewOrientation::FollowPhone);sequence(p,0,0); }
void repeat() { auto p=at(portrait);p.selectRotation(1);for(int i=0;i<2000;++i){p.setSourceSize(portrait);require(p.rotation()==1 && p.viewSize()==landscape,"duplicate update rotated again");} }
void resolution() { auto p=at(portrait);p.selectRotation(1);for(const auto &s:{QSize(1080,2200),QSize(2300,1080),QSize(720,1600),QSize(1600,720)}){p.setSourceSize(s);require(p.viewSize().width()>p.viewSize().height(),"resolution changed target axis");} }
void square() { auto p=at(portrait);p.selectRotation(3);p.setSourceSize(QSize(1080,1080));require(p.rotation()==3,"square invented source rotation");p.setSourceSize(landscape);require(p.rotation()==0,"square broke native landscape"); }
void invalid() { auto p=at(portrait);p.selectRotation(1);for(const auto &s:{QSize(),QSize(0,1),QSize(2,0),QSize(-1,10)}){require(!p.setSourceSize(s),"invalid source accepted");require(p.sourceSize()==portrait && p.rotation()==1,"invalid source erased preference");} }
void isolated() { auto a=at(portrait), b=at(portrait);a.selectRotation(1);b.selectRotation(3);a.setSourceSize(landscape);require(a.rotation()==0 && b.rotation()==3 && b.sourceSize()==portrait,"cross-window preference leak"); }
void afterTransition() { auto p=at(portrait);p.selectRotation(1);p.setSourceSize(landscape);p.selectRotation(p.rotation()+1);require(p.viewSize()==portrait,"manual turn used stale source");sequence(p,0,1); }
void manualCycle() { auto p=at(portrait);for(int i=1;i<=12;++i){p.selectRotation(p.rotation()+1);require(p.rotation()==i%4,"manual 90 sequence changed");require(p.viewSize()==(i%2?landscape:portrait),"manual cycle axis wrong");} }
void mapping() { auto p=at(portrait);p.selectRotation(1);require(qsc::ViewGeometry::toSource(QPointF(0,0),p.viewSize(),p.rotation())==QPointF(0,2339),"portrait view touch wrong");p.setSourceSize(landscape);require(qsc::ViewGeometry::toSource(QPointF(0,0),p.viewSize(),p.rotation())==QPointF(0,0),"game native touch double-rotated");p.setSourceSize(portrait);require(qsc::ViewGeometry::toSource(QPointF(2339,1079),p.viewSize(),p.rotation())==QPointF(1079,0),"return touch wrong"); }
void wheel() { auto p=at(portrait);p.selectRotation(1);require(qsc::ViewGeometry::deltaToSource(QPoint(0,120),p.rotation())==QPoint(120,0),"portrait wheel wrong");p.setSourceSize(landscape);require(qsc::ViewGeometry::deltaToSource(QPoint(0,120),p.rotation())==QPoint(0,120),"landscape wheel double-rotated"); }
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    const struct { const char *name; void(*run)(); } cases[]={{"follow",follow},{"clockwise_game",clockwise},{"counterclockwise_game",counterclockwise},{"native_landscape",nativeLandscape},{"keep_portrait",keepPortrait},{"half_turn",halfTurn},{"reset",reset},{"repeat",repeat},{"resolution",resolution},{"square",square},{"invalid",invalid},{"isolation",isolated},{"after_transition",afterTransition},{"manual_cycle",manualCycle},{"mapping",mapping},{"wheel",wheel}};
    for (const auto &test:cases) if(argc==2 && QString::fromUtf8(argv[1])==test.name) {
        try { test.run(); std::printf("PASS %s\n",test.name); return 0; }
        catch(const std::exception &e) { std::fprintf(stderr,"FAIL %s: %s\n",test.name,e.what());return 1; }
    }
    return 2;
}
