#ifndef ACTIONMACRODIALOG_H
#define ACTIONMACRODIALOG_H
#include <QDialog>
#include <QPointer>
#include "../QtScrcpyCore/include/QtScrcpyCore.h"
class QLabel;class QPushButton;class QCloseEvent;class MacroExecutionOptions;
class ActionMacroDialog : public QDialog {
 Q_OBJECT
public:
 explicit ActionMacroDialog(const QString &serial,QWidget *parent=Q_NULLPTR);
 ~ActionMacroDialog() override;
 void reject() override;
protected:
 void closeEvent(QCloseEvent *event) override;
private slots:
 void toggleRecording();void saveMacro();void loadMacro();void playMacro();void togglePause();void editKeymap();void emergencyStop();
 void updateState(bool recording,bool playing,int eventCount);
 void updateProgress(int currentEvent,int totalEvents,int currentLoop,int totalLoops);
 void showError(const QString &message);
private:
 QPointer<qsc::IDevice> device() const;
 void refreshFromDevice();bool confirmDiscard();void markDisconnected();
 QString m_serial,m_currentFile,m_lastError;QPointer<qsc::IDevice> m_device;
 bool m_connected=false,m_dirty=false;
 QLabel *m_statusLabel=nullptr,*m_progressLabel=nullptr,*m_errorLabel=nullptr,*m_elapsedLabel=nullptr;
 QPushButton *m_recordButton=nullptr,*m_saveButton=nullptr,*m_loadButton=nullptr,*m_playButton=nullptr,*m_stopButton=nullptr,*m_pauseButton=nullptr,*m_keymapButton=nullptr;
 MacroExecutionOptions *m_options=nullptr;
};
#endif
