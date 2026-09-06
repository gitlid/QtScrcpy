#include <QApplication>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QWebEnginePage>
#include <cstdio>
#include "webkeymapdialog.h"
#include "../QtScrcpy/ui/keymapdocument.h"
int main(int argc,char **argv){
    qInstallMessageHandler([](QtMsgType,const QMessageLogContext&,const QString &text){fprintf(stderr,"%s\n",text.toUtf8().constData());fflush(stderr);});
    WebKeymapDialog::registerScheme();QApplication app(argc,argv);
    QImage image(1280,720,QImage::Format_RGB32);image.fill(QColor(48,55,66));
    const QString config=R"({"switchKey":"Key_QuoteLeft","mouseLookEnabled":false,"ownerNote":"roundtrip","keyMapNodes":[{"type":"KMT_CLICK","key":"BackButton","pos":{"x":0.8,"y":0.5},"switchMap":false,"customTest":42},{"type":"KMT_STEER_WHEEL","centerPos":{"x":0.2,"y":0.6},"leftKey":"Key_A","rightKey":"Key_D","upKey":"Key_W","downKey":"Key_S","leftOffset":0.1,"rightOffset":0.1,"upOffset":0.1,"downOffset":0.1}]})";
    WebKeymapDialog editor(image,config);editor.show();bool finished=false;
    QTimer::singleShot(35000,&app,[&]{if(!finished){const QString shot=qEnvironmentVariable("QSC_KEYMAPPER_SCREENSHOT");if(!shot.isEmpty())editor.grab().save(shot);qCritical()<<"Editor bridge timed out";app.exit(1);}});
    QObject::connect(&editor,&WebKeymapDialog::editorReady,&app,[&]{
        QTimer::singleShot(700,&app,[&]{
            editor.readConfiguration([&](QString text){
                const auto object=QJsonDocument::fromJson(text.toUtf8()).object();KeymapDocument checked;QString reason;
                const bool ok=checked.parse(text.toUtf8(),&reason)&&object["ownerNote"]=="roundtrip"&&object["mouseLookEnabled"]==false&&object["keyMapNodes"].toArray().size()==2&&object["keyMapNodes"].toArray()[0].toObject()["customTest"]==42;
                if(!ok){qCritical()<<"Roundtrip failed"<<reason<<text;finished=true;app.exit(2);return;}
                const QString exercise=QString::fromUtf8(R"JS((()=>{try{
                    const passed=[];const check=(name,ok)=>{if(!ok)throw Error(name);passed.push(name);};
                    check('upstream_two_nodes',nodeManager.nodes.length===2);
                    check('upstream_four_way_style',Object.keys(nodeManager.nodes[1].shape.directionButtons).length===4);
                    check('offline_origin',location.protocol==='qsc-keymapper:');
                    nodeManager.selectNode(nodeManager.nodes[0]);
                    const field=document.getElementById('mappingKeyProperties');field.focus();
                    field.dispatchEvent(new MouseEvent('mousedown',{button:4,bubbles:true,cancelable:true}));
                    check('side_button_capture',nodeManager.nodes[0].mappingData.key==='ForwardButton');
                    field.dispatchEvent(new KeyboardEvent('keydown',{key:'F7',code:'F7',bubbles:true,cancelable:true}));
                    check('keyboard_capture',nodeManager.nodes[0].mappingData.key==='Key_F7');
                    field.focus();document.querySelector('[data-mouse="LeftButton"]').click();
                    check('explicit_left_button',nodeManager.nodes[0].mappingData.key==='LeftButton');
                    document.body.dispatchEvent(new MouseEvent('mousedown',{button:0,bubbles:true}));
                    check('no_global_click_binding',nodeManager.nodes[0].mappingData.key==='LeftButton');
                    const old=qscEditor.exportConfig();old.keyMapNodes[0].key='BackButton';
                    old.keyMapNodes.push({type:'KMT_CLICK_TWICE',key:'Key_E',pos:{x:.7,y:.25},customData:3});
                    old.keyMapNodes.push({type:'KMT_DRAG',key:'ForwardButton',startPos:{x:.6,y:.7},endPos:{x:.8,y:.6},startDelay:100,dragSpeed:.5});
                    old.keyMapNodes.push({type:'KMT_CLICK_MULTI',key:'Key_F',clickNodes:[{pos:{x:.4,y:.3},delay:100},{pos:{x:.5,y:.4},delay:200}]});
                    old.keyMapNodes.push({type:'KMT_ANDROID_KEY',key:'Key_F8',androidKey:3,note:'opaque'});
                    old.mouseMoveMap={startPos:{x:.5,y:.5},speedRatio:1,customView:'keep'};
                    qscEditor.importConfig(old);const result=qscEditor.exportConfig();
                    check('six_upstream_visual_nodes',nodeManager.nodes.length===6);
                    check('opaque_android_preserved',result.keyMapNodes.some(n=>n.type==='KMT_ANDROID_KEY'&&n.note==='opaque'));
                    check('extension_metadata_preserved',result.ownerNote==='roundtrip'&&result.keyMapNodes[0].customTest===42&&result.mouseMoveMap.customView==='keep');
                    check('view_explicitly_off',result.mouseLookEnabled===false);
                    check('multi_delay_roundtrip',result.keyMapNodes.find(n=>n.type==='KMT_CLICK_MULTI').clickNodes[1].delay===200);
                    const before=JSON.stringify(result);try{qscEditor.importConfig({});}catch(e){}
                    check('invalid_import_transaction',JSON.stringify(qscEditor.exportConfig())===before);
                    const canvas=document.getElementById('mappingCanvas').getBoundingClientRect();const aside=document.querySelector('aside').getBoundingClientRect();
                    check('canvas_and_sidebar_fit',canvas.bottom<=innerHeight&&aside.bottom<=innerHeight&&aside.left>=canvas.right-20);
                    return JSON.stringify({passed,config:qscEditor.exportConfig()});
                }catch(e){return JSON.stringify({error:String(e)});}})())JS");
                editor.page()->runJavaScript(exercise,[&](QVariant result){
                    const auto value=QJsonDocument::fromJson(result.toString().toUtf8()).object();KeymapDocument document;QString error;
                    const bool good=!value.contains("error")&&value["passed"].toArray().size()==14&&document.parse(QJsonDocument(value["config"].toObject()).toJson(),&error);
                    const QString shot=qEnvironmentVariable("QSC_KEYMAPPER_SCREENSHOT");if(!shot.isEmpty())editor.grab().save(shot);
                    qInfo()<<"Native offline upstream editor"<<(good?"PASS":"FAIL")<<error<<result;finished=true;app.exit(good?0:3);
                });
            });
        });
    });
    return app.exec();
}
