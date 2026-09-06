#include <QApplication>
#include <QGraphicsView>
#include <QPainter>
#include <QTimer>
#include <QEventLoop>
#include <QDebug>
#include "keymapeditor.h"
int main(int argc,char**argv){
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    const QString mode=argc>1?argv[1]:"portrait";
    const QSize size=mode=="landscape"?QSize(800,400):QSize(400,800);
    QPixmap frame(size);frame.fill(QColor(40,50,65));
    { QPainter p(&frame);p.setPen(QPen(QColor(80,95,110),1));for(int x=0;x<size.width();x+=40)p.drawLine(x,0,x,size.height());for(int y=0;y<size.height();y+=40)p.drawLine(0,y,size.width(),y); }
    KeymapDocument doc;doc.add("KMT_CLICK");
    KeymapEditor editor(frame,QString::fromUtf8(QJsonDocument(doc.root).toJson()));editor.show();
    auto wait=[](){QEventLoop loop;QTimer::singleShot(80,&loop,&QEventLoop::quit);loop.exec();};wait();
    if(mode=="resized"){editor.resize(1250,900);wait();}
    auto*view=editor.findChild<QGraphicsView*>();if(!view)return 1;
    const QRect mapped=view->mapFromScene(view->scene()->sceneRect()).boundingRect();const QSize viewport=view->viewport()->size();
    const bool fills=mapped.width()>viewport.width()*0.9||mapped.height()>viewport.height()*0.9;
    const bool fits=mapped.width()<=viewport.width()+3&&mapped.height()<=viewport.height()+3;
    if(qEnvironmentVariableIsSet("QSC_TEST_SCREENSHOT"))editor.grab().save(qEnvironmentVariable("QSC_TEST_SCREENSHOT"));
    editor.done(QDialog::Rejected);
    qInfo()<<"mapping canvas"<<mode<<mapped<<viewport<<"fits"<<fits<<"fills"<<fills;
    return fits&&fills?0:1;
}
