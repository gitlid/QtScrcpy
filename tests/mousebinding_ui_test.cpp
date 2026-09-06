#include <QApplication>
#include <QAction>
#include <QContextMenuEvent>
#include <QDebug>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <functional>
#include "inputbindingfield.h"
#include "keymapdocument.h"
#include "keymapeditor.h"

namespace {
void click(QWidget *field, Qt::MouseButton b) {
    QMouseEvent press(QEvent::MouseButtonPress,QPointF(15,10),b,b,Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease,QPointF(15,10),b,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(field,&press);QApplication::sendEvent(field,&release);
}
bool menuButtons() {
    InputBindingField field;field.show();
    auto *menu=field.findChild<QMenu*>("mouseButtonsMenu");if(!menu||menu->actions().size()!=5)return false;
    for(auto *a:menu->actions()){a->trigger();if(field.text()!=a->data().toString())return false;}
    return true;
}
bool directButtons() {
    InputBindingField field;field.show();
    for(auto b:{Qt::RightButton,Qt::MiddleButton,Qt::BackButton,Qt::ForwardButton,Qt::ExtraButton4}) {
        click(&field,b);if(InputBinding::mouseButton(field.text())!=int(b))return false;
    }
    QContextMenuEvent context(QContextMenuEvent::Mouse,QPoint(5,5));QApplication::sendEvent(&field,&context);
    return !field.findChild<QMenu*>("mouseButtonsMenu")->isVisible();
}
bool focusDoesNotBindLeft() {
    InputBindingField field;field.setText("BackButton");field.show();click(&field,Qt::LeftButton);
    return field.text()=="BackButton";
}
bool keyboardStillWorks() {
    InputBindingField field;QKeyEvent press(QEvent::KeyPress,Qt::Key_F7,Qt::NoModifier);
    QApplication::sendEvent(&field,&press);if(field.text()!="Key_F7")return false;
    QKeyEvent tab(QEvent::KeyPress,Qt::Key_Tab,Qt::NoModifier);QApplication::sendEvent(&field,&tab);
    return field.text()=="Key_Tab";
}
bool aliasesConflict() {
    for(const auto &alias:QStringList{"XButton1","ExtraButton1"}){
        KeymapDocument d;d.add("KMT_CLICK");d.add("KMT_CLICK");auto a=d.nodes()[0].toObject();a["key"]="BackButton";d.setNode(0,a);
        a=d.nodes()[1].toObject();a["key"]=alias;d.setNode(1,a);if(KeymapDocument::validate(d.root))return false;
    }
    KeymapDocument d;d.add("KMT_CLICK");auto n=d.nodes()[0].toObject();n["key"]="ForwardButton";d.setNode(0,n);d.root["switchKey"]="XButton2";
    return !KeymapDocument::validate(d.root);
}
bool invalidButtons() {
    for(const auto &name:QStringList{"NoButton","AllButtons","MouseButtonMask","LeftButton|RightButton","unknown","Key_unknown"})
        if(KeymapDocument::binding(name))return false;
    return KeymapDocument::binding("LeftButton")&&KeymapDocument::binding("ExtraButton24");
}
bool deviceNamespaces() {
    KeymapDocument d;d.add("KMT_CLICK");d.add("KMT_CLICK");auto n=d.nodes()[1].toObject();n["key"]="TaskButton";d.setNode(1,n);
    return KeymapDocument::validate(d.root)&&InputBinding::identity("TaskButton")!=InputBinding::identity("Key_Space");
}
bool editorRoundtrip() {
    KeymapDocument d;d.add("KMT_CLICK");QPixmap frame(400,800);frame.fill(Qt::darkGray);
    KeymapEditor editor(frame,QString::fromUtf8(QJsonDocument(d.root).toJson()));editor.show();QApplication::processEvents();
    auto *field=editor.findChild<QLineEdit*>("mappingBinding0");if(!field)return false;
    click(field,Qt::BackButton);KeymapDocument loaded;bool ok=loaded.parse(editor.script().toUtf8())&&loaded.nodes()[0].toObject()["key"]=="BackButton";
    auto *menu=field->findChild<QMenu*>("mouseButtonsMenu");if(!menu)return false;
    menu->actions()[0]->trigger();ok=ok&&loaded.parse(editor.script().toUtf8())&&loaded.nodes()[0].toObject()["key"]=="LeftButton";
    click(field,Qt::ForwardButton);QApplication::processEvents();
    if(qEnvironmentVariableIsSet("QSC_MOUSE_SCREENSHOT"))editor.grab().save(qEnvironmentVariable("QSC_MOUSE_SCREENSHOT"));
    editor.done(QDialog::Rejected);return ok;
}
bool toggleRouting() {
    for(const QString &name:QStringList{"LeftButton","RightButton","MiddleButton","BackButton","ForwardButton"}){
        KeymapDocument d;d.root["switchKey"]=name;const auto source=QString::fromUtf8(QJsonDocument(d.root).toJson());
        if(!InputBinding::isMouseSwitch(source,Qt::MouseButton(InputBinding::mouseButton(name))))return false;
    }
    return !InputBinding::isMouseSwitch("{}",Qt::NoButton)&&!InputBinding::isMouseSwitch("broken",Qt::RightButton);
}
}
int main(int argc,char **argv) {
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    const QVector<QPair<QString,std::function<bool()>>> tests{
        {"menu",menuButtons},{"direct",directButtons},{"left_focus",focusDoesNotBindLeft},{"keyboard",keyboardStillWorks},
        {"alias_conflict",aliasesConflict},{"invalid",invalidButtons},{"namespaces",deviceNamespaces},
        {"editor_roundtrip",editorRoundtrip},{"toggle_routing",toggleRouting}
    };
    int ran=0,passed=0;for(const auto &t:tests){if(argc>1&&QString(argv[1])!=t.first)continue;++ran;bool ok=t.second();if(ok)++passed;qInfo()<<t.first<<(ok?"PASS":"FAIL");}
    return ran>0&&passed==ran?0:1;
}
