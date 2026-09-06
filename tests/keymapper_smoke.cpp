#include <QApplication>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QWebEnginePage>
#include "webkeymapdialog.h"
int main(int argc,char **argv){
    WebKeymapDialog::registerScheme();QApplication app(argc,argv);
    QImage image(1280,720,QImage::Format_RGB32);image.fill(QColor(48,55,66));
    const QString config=R"({"switchKey":"Key_QuoteLeft","mouseLookEnabled":false,"ownerNote":"roundtrip","keyMapNodes":[{"type":"KMT_CLICK","key":"BackButton","pos":{"x":0.8,"y":0.5},"switchMap":false,"customTest":42},{"type":"KMT_STEER_WHEEL","centerPos":{"x":0.2,"y":0.6},"leftKey":"Key_A","rightKey":"Key_D","upKey":"Key_W","downKey":"Key_S","leftOffset":0.1,"rightOffset":0.1,"upOffset":0.1,"downOffset":0.1}]})";
    WebKeymapDialog editor(image,config);editor.show();bool finished=false;
    QTimer::singleShot(35000,&app,[&]{if(!finished){qCritical()<<"Editor bridge timed out";app.exit(1);}});
    QObject::connect(&editor,&WebKeymapDialog::editorReady,&app,[&]{
        QTimer::singleShot(700,&app,[&]{
            editor.readConfiguration([&](QString text){
                const auto object=QJsonDocument::fromJson(text.toUtf8()).object();
                const bool ok=object["ownerNote"]=="roundtrip"&&object["mouseLookEnabled"]==false&&object["keyMapNodes"].toArray().size()==2;
                if(!ok){qCritical()<<"Roundtrip failed"<<text;finished=true;app.exit(2);return;}
                editor.page()->runJavaScript("JSON.stringify({nodes:nodeManager.nodes.length,side:nodeManager.nodes[0].mappingData.key,shape:nodeManager.nodes[1].shape.directionButtons!==undefined,offline:location.protocol})",[&](QVariant result){
                    const auto value=QJsonDocument::fromJson(result.toString().toUtf8()).object();
                    const bool good=value["nodes"].toInt()==2&&value["side"]=="BackButton"&&value["shape"].toBool()&&value["offline"]=="qsc-keymapper:";
                    const QString shot=qEnvironmentVariable("QSC_KEYMAPPER_SCREENSHOT");if(!shot.isEmpty())editor.grab().save(shot);
                    qInfo()<<"Native offline upstream editor bridge"<<(good?"PASS":"FAIL")<<result;finished=true;app.exit(good?0:3);
                });
            });
        });
    });
    return app.exec();
}
