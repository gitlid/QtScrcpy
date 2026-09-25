#include <QApplication>
#include <QImage>
#include <QColor>
#include <QTimer>
#include <QEventLoop>
#include <cstdio>
#include "../../QtScrcpy/render/qyuvopenglwidget.h"
#include "../../QtScrcpy/QtScrcpyCore/include/viewgeometry.h"

static void wait() { QEventLoop loop; QTimer::singleShot(100,&loop,&QEventLoop::quit); loop.exec(); }
int main(int argc,char **argv) {
    QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    QApplication app(argc,argv); app.setQuitOnLastWindowClosed(false);
    QYUVOpenGLWidget view; view.resize(320,160); view.show(); wait();
    if(!view.isValid()){std::fprintf(stderr,"OpenGL context unavailable\n");return 1;}
    QByteArray y(64*32,0),u(32*16,0),v(32*16,0);
    const int ys[4]={63,173,32,235},us[4]={102,42,240,128},vs[4]={240,26,118,128};
    for(int row=0;row<32;++row)for(int col=0;col<64;++col)y[row*64+col]=char(ys[(row>=16?2:0)+(col>=32?1:0)]);
    for(int row=0;row<16;++row)for(int col=0;col<32;++col){const int q=(row>=8?2:0)+(col>=16?1:0);u[row*32+col]=char(us[q]);v[row*32+col]=char(vs[q]);}
    view.setFrameSize(QSize(64,32)); wait();
    view.updateTextures(reinterpret_cast<quint8 *>(y.data()),reinterpret_cast<quint8 *>(u.data()),reinterpret_cast<quint8 *>(v.data()),64,32,32); wait();
    const QImage original=view.grabFramebuffer();
    if(original.isNull() || original.pixelColor(original.width()/4,original.height()/4).red()<150){std::fprintf(stderr,"No valid decoded texture\n");return 1;}
    for(int turn=0;turn<4;++turn){
        view.resize(turn%2?160:320,turn%2?320:160); view.setViewRotation(turn); wait();
        const QImage rotated=view.grabFramebuffer();
        for(int row=0;row<2;++row)for(int col=0;col<2;++col){
            const QPointF unit(.25+.5*col,.25+.5*row),src=qsc::ViewGeometry::inverseUnit(unit,turn);
            const QColor a=rotated.pixelColor(int(unit.x()*rotated.width()),int(unit.y()*rotated.height()));
            const QColor b=original.pixelColor(int(src.x()*original.width()),int(src.y()*original.height()));
            if(qAbs(a.red()-b.red())>8||qAbs(a.green()-b.green())>8||qAbs(a.blue()-b.blue())>8){std::fprintf(stderr,"Texture/input inverse disagreement at turn %d\n",turn);return 1;}
        }
        if(turn==1 && qEnvironmentVariableIsSet("QSC_VIEW_SCREENSHOT")) rotated.save(qEnvironmentVariable("QSC_VIEW_SCREENSHOT"));
        if(view.frameSize()!=QSize(64,32)){std::fprintf(stderr,"View rotation changed decoder frame size\n");return 1;}
    }
    std::fprintf(stdout,"PASS native OpenGL 0/90/180/270 texture orientation\n"); return 0;
}
