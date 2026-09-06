#ifndef ACTIONMACRODIALOG_H
#define ACTIONMACRODIALOG_H

#include <QDialog>
#include <QPointer>
#include "../QtScrcpyCore/include/QtScrcpyCore.h"

class QLabel;
class QPushButton;
class QSpinBox;
class QCloseEvent;

class ActionMacroDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ActionMacroDialog(const QString &serial, QWidget *parent = Q_NULLPTR);
    ~ActionMacroDialog() override;
    void reject() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void toggleRecording();
    void saveMacro();
    void loadMacro();
    void playMacro();
    void emergencyStop();
    void updateState(bool recording, bool playing, int eventCount);
    void updateProgress(int currentEvent, int totalEvents, int currentLoop, int totalLoops);
    void showError(const QString &message);

private:
    QPointer<qsc::IDevice> device() const;
    void refreshFromDevice();
    bool confirmDiscard();
    void markDisconnected();

    QString m_serial;
    QString m_currentFile;
    QString m_lastError;
    QPointer<qsc::IDevice> m_device;
    bool m_connected = false;
    bool m_dirty = false;
    QLabel *m_statusLabel = Q_NULLPTR;
    QLabel *m_progressLabel = Q_NULLPTR;
    QLabel *m_errorLabel = Q_NULLPTR;
    QPushButton *m_recordButton = Q_NULLPTR;
    QPushButton *m_saveButton = Q_NULLPTR;
    QPushButton *m_loadButton = Q_NULLPTR;
    QPushButton *m_playButton = Q_NULLPTR;
    QPushButton *m_stopButton = Q_NULLPTR;
    QSpinBox *m_repeatSpin = Q_NULLPTR;
    QSpinBox *m_intervalSpin = Q_NULLPTR;
};
#endif
