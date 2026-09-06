#include "actionmacrodialog.h"
#include "actionmacrohotkey.h"
#include "macroexecutionoptions.h"
#include "keymapeditor.h"
#ifdef QSC_WITH_KEYMAPPER
#include "webkeymapdialog.h"
#endif
#include <QOpenGLWidget>
#include <QTimer>
#include <QCloseEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

ActionMacroDialog::ActionMacroDialog(const QString &serial,QWidget *parent):QDialog(parent),m_serial(serial)
{
    setWindowTitle(tr("Action Macro 0.3.0-rc.1 - %1").arg(serial));
    setAttribute(Qt::WA_DeleteOnClose,false);
    resize(580,700);
    m_statusLabel=new QLabel(this);
    m_progressLabel=new QLabel(tr("No playback in progress."),this);
    m_errorLabel=new QLabel(this);
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setTextFormat(Qt::PlainText);
    m_errorLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_recordButton=new QPushButton(tr("Start recording"),this);
    m_saveButton=new QPushButton(tr("Save..."),this);
    m_loadButton=new QPushButton(tr("Load..."),this);
    m_playButton=new QPushButton(tr("Play"),this);
    m_stopButton=new QPushButton(tr("Emergency stop"),this);
    m_pauseButton=new QPushButton(tr("暂停"),this);
    m_pauseButton->setObjectName("pauseActionMacro");
    m_keymapButton=new QPushButton(tr("按键映射设置"),this);
    m_keymapButton->setObjectName("openKeymapEditor");
    m_options=new MacroExecutionOptions(this);
    m_elapsedLabel=new QLabel(this);
    auto *elapsedTimer=new QTimer(this);
    elapsedTimer->setInterval(100);
    connect(elapsedTimer,&QTimer::timeout,this,[this](){
        const auto d=device();
        if(d)m_elapsedLabel->setText(tr("有效运行：%1 秒（包含循环间隔，不包含暂停）").arg(d->actionMacroElapsedMs()/1000.0,0,'f',1));
    });
    elapsedTimer->start();
    auto changedOptions=[this](){if(m_device&&m_device->actionMacroEventCount()>0)m_dirty=true;};
    connect(m_options->mode,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[changedOptions](int){changedOptions();});
    connect(m_options->speed,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[changedOptions](int){changedOptions();});
    connect(m_options->count,QOverload<int>::of(&QSpinBox::valueChanged),this,[changedOptions](int){changedOptions();});
    connect(m_options->seconds,QOverload<int>::of(&QSpinBox::valueChanged),this,[changedOptions](int){changedOptions();});
    connect(m_options->interval,QOverload<double>::of(&QDoubleSpinBox::valueChanged),this,[changedOptions](double){changedOptions();});
    auto *hotkey=ActionMacroHotkey::instance();
    auto *hotkeyLabel=new QLabel(hotkey->globalAvailable()
        ?tr("Ctrl+Shift+X: Windows global emergency stop (all connected macro windows).")
        :tr("Ctrl+Shift+X: application-only stop. Global registration is unavailable; keep QtScrcpy focused or use Emergency stop."),this);
    hotkeyLabel->setWordWrap(true);
    auto *hint=new QLabel(tr("Restore the starting page before playback. Keep the same device, display size and orientation.\nMacro files may contain typed or clipboard text. Do not record passwords or share sensitive macros."),this);
    hint->setWordWrap(true);
    auto *fileButtons=new QHBoxLayout;
    fileButtons->addWidget(m_recordButton);fileButtons->addWidget(m_saveButton);fileButtons->addWidget(m_loadButton);
    auto *playButtons=new QHBoxLayout;
    playButtons->addWidget(m_playButton);playButtons->addWidget(m_pauseButton);playButtons->addWidget(m_stopButton);
    auto *layout=new QVBoxLayout(this);
    layout->addWidget(m_statusLabel);layout->addLayout(fileButtons);layout->addWidget(m_keymapButton);
    layout->addWidget(m_options);layout->addLayout(playButtons);layout->addWidget(m_progressLabel);
    layout->addWidget(m_elapsedLabel);layout->addWidget(m_errorLabel);layout->addWidget(hotkeyLabel);layout->addWidget(hint);
    auto *pauseHint=new QLabel(hotkey->pauseGlobalAvailable()
        ?tr("Ctrl+Shift+P：全局暂停。继续请点击本窗口按钮；中断长按/拖动时需要确认跳过当前动作组。")
        :tr("Ctrl+Shift+P：应用内暂停（全局注册失败）。继续请点击本窗口按钮。"),this);
    pauseHint->setWordWrap(true);layout->addWidget(pauseHint);
    auto *speedHint=new QLabel(tr("倍率仅缩放脚本时序，不会加速手机应用；循环间隔不缩放。8× 可能使长按或页面等待过短。"),this);
    speedHint->setWordWrap(true);layout->addWidget(speedHint);
    connect(m_recordButton,&QPushButton::clicked,this,&ActionMacroDialog::toggleRecording);
    connect(m_saveButton,&QPushButton::clicked,this,&ActionMacroDialog::saveMacro);
    connect(m_loadButton,&QPushButton::clicked,this,&ActionMacroDialog::loadMacro);
    connect(m_playButton,&QPushButton::clicked,this,&ActionMacroDialog::playMacro);
    connect(m_pauseButton,&QPushButton::clicked,this,&ActionMacroDialog::togglePause);
    connect(m_keymapButton,&QPushButton::clicked,this,&ActionMacroDialog::editKeymap);
    connect(m_stopButton,&QPushButton::clicked,this,&ActionMacroDialog::emergencyStop);
    connect(qApp,&QCoreApplication::aboutToQuit,this,&ActionMacroDialog::emergencyStop);
    // Bind to the original session, never a replacement with the same serial.
    m_device=qsc::IDeviceManage::getInstance().getDevice(m_serial);
    m_connected=!m_device.isNull();
    if(m_device){
        hotkey->watch(m_device.data());
        connect(m_device.data(),&qsc::IDevice::actionMacroStateChanged,this,&ActionMacroDialog::updateState);
        connect(m_device.data(),&qsc::IDevice::actionMacroProgress,this,&ActionMacroDialog::updateProgress);
        connect(m_device.data(),&qsc::IDevice::actionMacroError,this,&ActionMacroDialog::showError);
        connect(m_device.data(),&qsc::IDevice::deviceDisconnected,this,[this](const QString&){markDisconnected();});
        connect(m_device.data(),&QObject::destroyed,this,[this](){markDisconnected();});
    }
    refreshFromDevice();
}
ActionMacroDialog::~ActionMacroDialog(){emergencyStop();}
QPointer<qsc::IDevice> ActionMacroDialog::device()const{return m_connected?m_device:QPointer<qsc::IDevice>();}
void ActionMacroDialog::markDisconnected(){m_connected=false;updateState(false,false,0);m_statusLabel->setText(tr("Device disconnected. Reconnect and open a new macro window."));m_progressLabel->setText(tr("Playback stopped; no automatic resume."));}
void ActionMacroDialog::refreshFromDevice(){const auto d=device();if(!d){markDisconnected();return;}updateState(d->isActionRecording(),d->isActionPlaying(),d->actionMacroEventCount());}
bool ActionMacroDialog::confirmDiscard(){
    if(!m_dirty)return true;
    const auto answer=QMessageBox::question(this,tr("Unsaved action macro"),tr("Save the current recording before continuing?"),QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Save);
    if(answer==QMessageBox::Cancel)return false;
    if(answer==QMessageBox::Save){saveMacro();return !m_dirty;}
    return true;
}
void ActionMacroDialog::toggleRecording(){
    auto d=device();if(!d){showError(tr("The device is no longer connected."));return;}
    if(d->isActionRecording()){d->stopActionRecording();return;}
    if(!confirmDiscard())return;d=device();if(!d)return;
    m_lastError.clear();m_errorLabel->clear();
    if(!d->startActionRecording()){if(m_lastError.isEmpty())showError(tr("Recording could not start. Wait for the video and stop playback first."));return;}
    m_currentFile.clear();m_dirty=true;m_progressLabel->setText(tr("Record in the video window. Click Stop recording when finished."));
}
void ActionMacroDialog::saveMacro(){
    auto d=device();if(!d){showError(tr("The device is no longer connected."));return;}
    const QString suggested=m_currentFile.isEmpty()?QString("action-macro.qsmacro.json"):m_currentFile;
    const QString path=QFileDialog::getSaveFileName(this,tr("Save action macro"),suggested,tr("QtScrcpy action macro (*.qsmacro.json);;JSON files (*.json)"));
    if(path.isEmpty())return;d=device();if(!d){showError(tr("Device disconnected before saving."));return;}
    QString error;if(!d->saveActionMacro(path,&error)){showError(error);return;}
    if(!MacroExecutionOptions::saveTo(path,m_options->json())){showError(tr("宏事件已保存，但执行设置写入失败。请重试保存。"));return;}
    m_currentFile=path;m_dirty=false;m_errorLabel->clear();m_statusLabel->setText(tr("Saved %1 events to %2").arg(d->actionMacroEventCount()).arg(path));
}
void ActionMacroDialog::loadMacro(){
    if(!device()){showError(tr("The device is no longer connected."));return;}
    if(!confirmDiscard())return;
    const QString path=QFileDialog::getOpenFileName(this,tr("Load action macro"),m_currentFile,tr("QtScrcpy action macro (*.qsmacro.json);;JSON files (*.json)"));
    if(path.isEmpty())return;const auto d=device();if(!d){showError(tr("Device disconnected before loading."));return;}
    QString error;if(!d->loadActionMacro(path,&error)){showError(error);return;}
    m_currentFile=path;m_errorLabel->clear();m_progressLabel->setText(tr("Loaded %1 events. Restore the starting page before Play.").arg(d->actionMacroEventCount()));
    if(!m_options->loadFrom(path))showError(tr("执行设置无效，已恢复为单次 1×；宏事件仍可使用。"));
    m_dirty=false;
}
void ActionMacroDialog::playMacro(){
    if(m_options->repeats()==0&&m_options->limitMs()==0&&QMessageBox::question(this,tr("Continuous playback"),tr("Run until stopped? Confirm that this is a safe test screen and that the emergency stop is accessible."),QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes)return;
    const auto d=device();if(!d){showError(tr("The device is no longer connected."));return;}
    m_lastError.clear();m_errorLabel->clear();
    if(!d->playActionMacroAdvanced(m_options->repeats(),m_options->intervalMs(),m_options->multiplier(),m_options->limitMs())&&m_lastError.isEmpty())showError(tr("Playback could not start. Load or record a macro, wait for video, and stop recording first."));
}
void ActionMacroDialog::editKeymap(){
    auto current=device();if(!current||current->isActionPlaying()||current->isActionRecording())return;
    QWidget *host=parentWidget();while(host&&!host->inherits("VideoForm"))host=host->parentWidget();
    QPointer<QOpenGLWidget> surface=host?host->findChild<QOpenGLWidget*>():nullptr;
    if(!surface){showError(tr("请在 Windows 投屏窗口的操作宏入口打开按键编辑器。"));return;}
    const QSize displaySize=surface->size();const QImage image=surface->grabFramebuffer();
    if(image.isNull()){showError(tr("尚未取得画面，请等待投屏显示后重试。"));return;}
    current->prepareKeymapEditing();
    #ifdef QSC_WITH_KEYMAPPER
    WebKeymapDialog editor(image,current->currentKeymapScript());
#else
    KeymapEditor editor(QPixmap::fromImage(image),current->currentKeymapScript());
#endif
    #ifdef QSC_WITH_KEYMAPPER
    connect(current.data(),&qsc::IDevice::deviceDisconnected,&editor,[&editor](const QString&){editor.invalidateSession();});
    connect(current.data(),&QObject::destroyed,&editor,[&editor](){editor.invalidateSession();});
#else
    connect(current.data(),&qsc::IDevice::deviceDisconnected,&editor,[&editor](const QString&){editor.done(QDialog::Rejected);});
    connect(current.data(),&QObject::destroyed,&editor,[&editor](){editor.done(QDialog::Rejected);});
#endif
    const int result=editor.exec();
    if(result==QDialog::Accepted&&current&&surface&&surface->size()==displaySize&&!current->isActionPlaying()&&!current->isActionRecording())current->updateScript(editor.script());
}
void ActionMacroDialog::togglePause(){
    auto d=device();if(!d)return;
    if(!d->isActionPaused()){if(!d->pauseActionMacro())showError(tr("当前没有可暂停的录制或回放。"));return;}
    if(d->actionMacroInterruptedInput()){
        const auto result=QMessageBox::question(this,tr("当前动作已中断"),tr("暂停已释放按键和触点。继续将跳过中断的动作组直到全部输入释放的边界，不会重新点击或恢复同一段拖拽。确认手机页面正确后继续？"),QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel);
        if(result!=QMessageBox::Yes)return;
    }
    d=device();if(d&&!d->resumeActionMacro())showError(tr("无法继续：请检查连接和显示尺寸。"));
}
void ActionMacroDialog::emergencyStop(){auto d=m_device;if(d)d->stopActionPlayback();if(d)d->stopActionRecording();}
void ActionMacroDialog::closeEvent(QCloseEvent*event){emergencyStop();if(!confirmDiscard()){event->ignore();return;}event->accept();}
void ActionMacroDialog::reject(){close();}
void ActionMacroDialog::updateState(bool recording,bool playing,int eventCount){
    const bool available=m_connected&&!m_device.isNull();if(recording)m_dirty=true;
    m_recordButton->setText(recording?tr("Stop recording"):tr("Start recording"));m_recordButton->setEnabled(available&&!playing);
    m_saveButton->setEnabled(available&&!recording&&!playing&&eventCount>0);m_loadButton->setEnabled(available&&!recording&&!playing);
    m_playButton->setEnabled(available&&!recording&&!playing&&eventCount>0);m_stopButton->setEnabled(available&&(recording||playing));
    m_options->setEnabled(available&&!recording&&!playing);m_keymapButton->setEnabled(available&&!recording&&!playing);
    const bool paused=available&&m_device->isActionPaused();m_pauseButton->setEnabled(available&&(recording||playing));m_pauseButton->setText(paused?tr("继续"):tr("暂停"));
    if(!available)m_statusLabel->setText(tr("Device disconnected."));
    else if(paused)m_statusLabel->setText(recording?tr("录制已暂停（暂停期间不记录）"):tr("回放已暂停（保留轮次与剩余等待）"));
    else if(recording)m_statusLabel->setText(tr("Recording... %1 events captured.").arg(eventCount));
    else if(playing)m_statusLabel->setText(tr("Playing %1 events...").arg(eventCount));
    else m_statusLabel->setText(tr("Ready. %1 events in memory.%2").arg(eventCount).arg(m_dirty?tr(" Unsaved changes."):QString()));
}
void ActionMacroDialog::updateProgress(int currentEvent,int totalEvents,int currentLoop,int totalLoops){const QString loops=totalLoops==0?tr("infinite"):QString::number(totalLoops);m_progressLabel->setText(tr("Loop %1/%2, event %3/%4").arg(currentLoop).arg(loops).arg(currentEvent).arg(totalEvents));}
void ActionMacroDialog::showError(const QString&message){m_lastError=message;m_errorLabel->setText(tr("Error: %1").arg(message));}
