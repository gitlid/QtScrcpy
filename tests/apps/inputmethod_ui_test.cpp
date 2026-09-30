#include <QApplication>
#include <QClipboard>
#include <QInputMethodEvent>
#include <QPushButton>
#include <QLabel>
#include <QEventLoop>
#include <QTimer>
#include <cstdio>
#include <stdexcept>
#include "../../QtScrcpy/ui/inputmethoddialog.h"
#include "../../QtScrcpy/ui/appsession.h"
#include "ime_device_stub.h"
namespace {
void require(bool b,const char*m){if(!b)throw std::runtime_error(m);}
void wait(){QEventLoop e;QTimer::singleShot(20,&e,&QEventLoop::quit);e.exec();}
class Commands final:public AppCommands {
public:
    QString tag,serial;QStringList args;
    void run(const QString&t,const QString&s,const QStringList&a,int)override{tag=t;serial=s;args=a;}
    void cancelAll()override{tag.clear();}
    void cancel(const QString&t)override{if(tag==t)tag.clear();}
};
struct Fixture{
    ImeDevice d;Commands commands;InputMethodDialog dialog;
    ImeComposeEdit *edit;QPushButton *send;
    Fixture():dialog(&d,nullptr,nullptr,&commands){
        edit=dialog.findChild<ImeComposeEdit *>("imeText");send=dialog.findChild<QPushButton *>("sendImeText");
        require(edit&&send,"native IME editor and Send present");dialog.show();wait();
    }
    void preedit(const QString&s){QInputMethodEvent e(s,{});QApplication::sendEvent(edit,&e);}
    void commit(const QString&s){QInputMethodEvent e;e.setCommitString(s);QApplication::sendEvent(edit,&e);}
};
void native(){Fixture f;require(f.edit->testAttribute(Qt::WA_InputMethodEnabled),"native IME enabled");require(!f.send->isEnabled(),"empty must not send");}
void preedit(){Fixture f;f.preedit("nihao");require(f.edit->composing()&&!f.send->isEnabled()&&f.d.pasted.isEmpty(),"preedit never leaks to phone");f.preedit(QString());require(!f.edit->composing()&&f.d.pasted.isEmpty(),"cancel without transmission");}
void commit(){Fixture f;const auto s=QString::fromUtf8("你好😀");f.preedit("nihao");f.commit(s);require(f.edit->toPlainText()==s&&!f.edit->composing()&&f.d.pasted.isEmpty(),"candidate held locally");f.send->click();require(f.d.pasted==QStringList{s}&&f.edit->toPlainText().isEmpty(),"explicit send exactly once");f.send->click();require(f.d.pasted.size()==1,"no duplicate empty send");}
void mixed(){Fixture f;f.commit(QString::fromUtf8("中文"));f.preedit("pinyin");require(!f.send->isEnabled(),"existing committed text cannot send unfinished candidate");f.preedit(QString());require(f.send->isEnabled(),"cancellation restores committed send");}
void failure(){Fixture f;f.d.succeed=false;f.commit(QString::fromUtf8("保留文字"));f.send->click();require(f.d.pasted.isEmpty()&&f.edit->toPlainText()==QString::fromUtf8("保留文字"),"failed send keeps draft");}
void clipboard(){Fixture f;const auto saved=QString::fromUtf8("PC剪贴板不参与");QApplication::clipboard()->setText(saved);f.commit(QString::fromUtf8("中文"));f.send->click();require(QApplication::clipboard()->text()==saved,"no PC clipboard replacement");}
void buttons(){Fixture f;for(int i=0;i<3;++i)f.dialog.findChild<QPushButton *>(QString("imeShortcut%1").arg(i))->click();require(f.d.shortcuts==QList<int>{0,1,2},"explicit shortcuts not raw key mapping");}
void busy(){Fixture f;f.commit("x");f.d.playActionMacro(1,0);require(!f.send->isEnabled(),"playing blocks send");f.d.pauseActionMacro();require(!f.send->isEnabled(),"paused still blocks");for(int i=0;i<3;++i)require(!f.dialog.findChild<QPushButton *>(QString("imeShortcut%1").arg(i))->isEnabled(),"busy shortcut blocked");f.d.stopActionPlayback();require(f.send->isEnabled(),"stop restores sending");f.d.startActionRecording();require(!f.send->isEnabled(),"recording blocked");}
void disconnect(){Fixture f;f.commit("kept");f.d.disconnectDevice();require(!f.send->isEnabled(),"closed session cannot send");f.send->click();require(f.d.pasted.isEmpty()&&f.edit->toPlainText()=="kept","no follow of replacement serial");}
void settings(){Fixture f;f.dialog.findChild<QPushButton *>("imeSettings0")->click();require(f.commands.serial==f.d.serial&&f.commands.args==QStringList{"shell","am","start","-a","android.settings.INPUT_METHOD_SETTINGS"},"settings scoped to device");f.commands.cancelAll();f.dialog.findChild<QPushButton *>("imeSettings1")->click();require(f.commands.args.last()=="android.settings.HARD_KEYBOARD_SETTINGS","physical settings not language guessing");}
void noauto(){Fixture f;f.commit("line");QKeyEvent down(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(f.edit,&down);require(f.d.pasted.isEmpty()&&f.edit->toPlainText().contains('\n'),"Enter is not automatic send");}
void screenshot(){Fixture f;f.edit->setPlainText(QString::fromUtf8("你好，这是电脑拼音输入。"));if(qEnvironmentVariableIsSet("QSC_IME_SCREENSHOT"))require(f.dialog.grab().save(qEnvironmentVariable("QSC_IME_SCREENSHOT")),"save IME screenshot");}
}
int main(int argc,char**argv){QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
const QVector<QPair<QString,std::function<void()>>> cases{{"native",native},{"preedit",preedit},{"commit",commit},{"mixed",mixed},{"failure",failure},{"clipboard",clipboard},{"shortcuts",buttons},{"busy",busy},{"disconnect",disconnect},{"settings",settings},{"enter",noauto},{"screenshot",screenshot}};
for(const auto&t:cases)if(argc==2&&QString(argv[1])==t.first){try{t.second();std::printf("PASS %s\n",qPrintable(t.first));return 0;}catch(const std::exception&e){std::fprintf(stderr,"FAIL %s: %s\n",qPrintable(t.first),e.what());return 1;}}return 2;}
