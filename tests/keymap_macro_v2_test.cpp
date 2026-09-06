#include <QApplication>
#include <QFile>
#include <QGraphicsEllipseItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <functional>
#include "keymapdocument.h"
#include "keymapeditor.h"
#include "macroexecutionoptions.h"
#include "actionmacrohotkey.h"
int main(int argc,char **argv){
 QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
 const QString selected=argc>1?argv[1]:"";int ran=0,passed=0;
 auto test=[&](const char*name,const std::function<bool()>&f){if(!selected.isEmpty()&&selected!=name)return;++ran;bool ok=f();if(ok)++passed;qInfo("%s %s",ok?"PASS":"FAIL",name);};
 test("settings_eight",[]{MacroExecutionOptions o;double highest=0;for(int i=0;i<o.speed->count();++i)highest=qMax(highest,o.speed->itemData(i).toDouble());o.speed->setCurrentIndex(o.speed->count()-1);o.mode->setCurrentIndex(1);o.seconds->setValue(600);o.interval->setValue(1.25);return highest==8.&&o.multiplier()==8.&&o.limitMs()==600000&&o.repeats()==0&&o.intervalMs()==1250;});
 test("settings_invalid",[]{MacroExecutionOptions o;auto j=o.json();j["speed"]=9;bool reject=!o.restore(j)&&o.multiplier()==1&&o.repeats()==1;j["speed"]=8;j["durationSeconds"]=-1;return reject&&!o.restore(j)&&o.multiplier()==1;});
 test("settings_roundtrip",[]{QTemporaryDir dir;QString path=dir.filePath("m.json");QFile f(path);if(!f.open(QIODevice::WriteOnly))return false;f.write("{\"format\":\"QtScrcpyActionMacro\",\"version\":2,\"events\":[]}");f.close();MacroExecutionOptions a;a.speed->setCurrentIndex(a.speed->count()-1);a.mode->setCurrentIndex(2);a.interval->setValue(.125);if(!MacroExecutionOptions::saveTo(path,a.json()))return false;MacroExecutionOptions b;return b.loadFrom(path)&&b.json()==a.json();});
 test("keymap_roundtrip",[]{KeymapDocument a;a.add("KMT_CLICK");a.add("KMT_STEER_WHEEL");a.add("KMT_DRAG");a.root["comment"]="preserve this";QString error;if(!KeymapDocument::validate(a.root,&error)){qWarning()<<error;return false;}KeymapDocument b;return b.parse(QJsonDocument(a.root).toJson())&&b.root==a.root;});
 test("keymap_conflicts",[]{KeymapDocument d;d.add("KMT_CLICK");d.add("KMT_CLICK");if(KeymapDocument::validate(d.root))return false;auto n=d.nodes()[1].toObject();n["key"]="Key_F2";d.setNode(1,n);if(!KeymapDocument::validate(d.root))return false;d.root["switchKey"]="Key_F2";return !KeymapDocument::validate(d.root);});
 test("keymap_bounds",[]{KeymapDocument d;d.add("KMT_CLICK");auto n=d.nodes()[0].toObject();n["pos"]=KeymapDocument::pos(1,.5);d.setNode(0,n);if(KeymapDocument::validate(d.root))return false;KeymapDocument wheel;wheel.add("KMT_STEER_WHEEL");auto w=wheel.nodes()[0].toObject();w["centerPos"]=KeymapDocument::pos(.01,.01);wheel.setNode(0,w);return !KeymapDocument::validate(wheel.root);});
 test("keymap_transaction",[]{KeymapDocument d;d.add("KMT_CLICK");const auto old=d.root;return !d.parse("not json")&&d.root==old&&!d.parse(QByteArray(1024*1024+1,'x'))&&d.root==old;});
 test("editor_drag",[]{
  QPixmap frame(400,800);frame.fill(Qt::darkGray);KeymapDocument d;d.add("KMT_CLICK");KeymapEditor e(frame,QString::fromUtf8(QJsonDocument(d.root).toJson()));e.show();QApplication::processEvents();
  auto*view=e.findChild<QGraphicsView*>();if(!view)return false;QGraphicsEllipseItem*marker=nullptr;for(auto*item:view->scene()->items())if(auto*p=dynamic_cast<QGraphicsEllipseItem*>(item)){marker=p;break;}if(!marker)return false;
  marker->setPos(100,200);auto nodes=QJsonDocument::fromJson(e.script().toUtf8()).object()["keyMapNodes"].toArray();auto pos=nodes[0].toObject()["pos"].toObject();bool ok=pos["x"].toDouble()==.25&&pos["y"].toDouble()==.25;
  marker->setPos(-20,1000);nodes=QJsonDocument::fromJson(e.script().toUtf8()).object()["keyMapNodes"].toArray();pos=nodes[0].toObject()["pos"].toObject();ok=ok&&pos["x"].toDouble()==0&&pos["y"].toDouble()<1;e.done(QDialog::Rejected);return ok;
 });
 test("pause_shortcut",[]{auto*h=ActionMacroHotkey::instance();Q_UNUSED(h);QWidget w;QKeyEvent e(QEvent::KeyPress,Qt::Key_P,Qt::ControlModifier|Qt::ShiftModifier);e.ignore();QApplication::sendEvent(&w,&e);return e.isAccepted();});
 qInfo("Keymap/Macro UI: %d/%d",passed,ran);return ran>0&&ran==passed?0:1;
}
