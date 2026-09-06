#include "actionmacrodialog.h"

#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

ActionMacroDialog::ActionMacroDialog(const QString &serial, QWidget *parent)
    : QDialog(parent)
    , m_serial(serial)
{
    setWindowTitle(tr("Action Macro - %1").arg(serial));
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(430, 230);

    m_statusLabel = new QLabel(this);
    m_progressLabel = new QLabel(this);
    m_progressLabel->setText(tr("No playback in progress."));

    m_recordButton = new QPushButton(tr("Start recording"), this);
    m_saveButton = new QPushButton(tr("Save..."), this);
    m_loadButton = new QPushButton(tr("Load..."), this);
    m_playButton = new QPushButton(tr("Play"), this);
    m_stopButton = new QPushButton(tr("Emergency stop"), this);
    m_stopButton->setToolTip(tr("Immediately stop playback and release held keys and touches (Ctrl+Shift+X)."));

    m_repeatSpin = new QSpinBox(this);
    m_repeatSpin->setRange(0, 9999);
    m_repeatSpin->setValue(1);
    m_repeatSpin->setSpecialValueText(tr("Until stopped"));
    m_intervalSpin = new QSpinBox(this);
    m_intervalSpin->setRange(0, 600000);
    m_intervalSpin->setValue(500);
    m_intervalSpin->setSuffix(tr(" ms"));

    QHBoxLayout *fileButtons = new QHBoxLayout;
    fileButtons->addWidget(m_recordButton);
    fileButtons->addWidget(m_saveButton);
    fileButtons->addWidget(m_loadButton);

    QFormLayout *options = new QFormLayout;
    options->addRow(tr("Repeat count:"), m_repeatSpin);
    options->addRow(tr("Loop interval:"), m_intervalSpin);

    QHBoxLayout *playbackButtons = new QHBoxLayout;
    playbackButtons->addWidget(m_playButton);
    playbackButtons->addWidget(m_stopButton);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(m_statusLabel);
    layout->addLayout(fileButtons);
    layout->addLayout(options);
    layout->addLayout(playbackButtons);
    layout->addWidget(m_progressLabel);

    connect(m_recordButton, &QPushButton::clicked, this, &ActionMacroDialog::toggleRecording);
    connect(m_saveButton, &QPushButton::clicked, this, &ActionMacroDialog::saveMacro);
    connect(m_loadButton, &QPushButton::clicked, this, &ActionMacroDialog::loadMacro);
    connect(m_playButton, &QPushButton::clicked, this, &ActionMacroDialog::playMacro);
    connect(m_stopButton, &QPushButton::clicked, this, &ActionMacroDialog::emergencyStop);

    QPointer<qsc::IDevice> currentDevice = device();
    if (currentDevice) {
        connect(currentDevice, &qsc::IDevice::actionMacroStateChanged, this, &ActionMacroDialog::updateState);
        connect(currentDevice, &qsc::IDevice::actionMacroProgress, this, &ActionMacroDialog::updateProgress);
        connect(currentDevice, &qsc::IDevice::actionMacroError, this, &ActionMacroDialog::showError);
        connect(currentDevice, &qsc::IDevice::deviceDisconnected, this, [this](const QString &) {
            refreshFromDevice();
        });
    }
    refreshFromDevice();
}

QPointer<qsc::IDevice> ActionMacroDialog::device() const
{
    return qsc::IDeviceManage::getInstance().getDevice(m_serial);
}

void ActionMacroDialog::refreshFromDevice()
{
    QPointer<qsc::IDevice> currentDevice = device();
    if (!currentDevice) {
        updateState(false, false, 0);
        m_statusLabel->setText(tr("Device disconnected."));
        return;
    }
    updateState(currentDevice->isActionRecording(), currentDevice->isActionPlaying(), currentDevice->actionMacroEventCount());
}

void ActionMacroDialog::toggleRecording()
{
    QPointer<qsc::IDevice> currentDevice = device();
    if (!currentDevice) {
        showError(tr("The device is no longer connected."));
        return;
    }
    if (currentDevice->isActionRecording()) {
        currentDevice->stopActionRecording();
        return;
    }
    if (currentDevice->actionMacroEventCount() > 0
        && QMessageBox::question(this, tr("Replace recording"),
                                 tr("Starting a new recording clears the current in-memory macro. Continue?")) != QMessageBox::Yes) {
        return;
    }
    m_currentFile.clear();
    if (!currentDevice->startActionRecording()) {
        showError(tr("Recording could not start. Stop playback first."));
    }
}

void ActionMacroDialog::saveMacro()
{
    QPointer<qsc::IDevice> currentDevice = device();
    if (!currentDevice) {
        showError(tr("The device is no longer connected."));
        return;
    }
    QString suggested = m_currentFile.isEmpty() ? QString("action-macro.qsmacro.json") : m_currentFile;
    const QString fileName = QFileDialog::getSaveFileName(this, tr("Save action macro"), suggested,
                                                           tr("QtScrcpy action macro (*.qsmacro.json);;JSON files (*.json)"));
    if (fileName.isEmpty()) {
        return;
    }
    QString error;
    if (!currentDevice->saveActionMacro(fileName, &error)) {
        showError(error);
        return;
    }
    m_currentFile = fileName;
    m_statusLabel->setText(tr("Saved %1 events to %2").arg(currentDevice->actionMacroEventCount()).arg(fileName));
}

void ActionMacroDialog::loadMacro()
{
    QPointer<qsc::IDevice> currentDevice = device();
    if (!currentDevice) {
        showError(tr("The device is no longer connected."));
        return;
    }
    const QString fileName = QFileDialog::getOpenFileName(this, tr("Load action macro"), m_currentFile,
                                                           tr("QtScrcpy action macro (*.qsmacro.json);;JSON files (*.json)"));
    if (fileName.isEmpty()) {
        return;
    }
    QString error;
    if (!currentDevice->loadActionMacro(fileName, &error)) {
        showError(error);
        return;
    }
    m_currentFile = fileName;
    m_progressLabel->setText(tr("Loaded %1 events.").arg(currentDevice->actionMacroEventCount()));
}

void ActionMacroDialog::playMacro()
{
    QPointer<qsc::IDevice> currentDevice = device();
    if (!currentDevice) {
        showError(tr("The device is no longer connected."));
        return;
    }
    if (!currentDevice->playActionMacro(m_repeatSpin->value(), m_intervalSpin->value())) {
        showError(tr("Playback could not start. Load or record a macro and stop recording first."));
    }
}

void ActionMacroDialog::emergencyStop()
{
    QPointer<qsc::IDevice> currentDevice = device();
    if (!currentDevice) {
        return;
    }
    currentDevice->stopActionPlayback();
    currentDevice->stopActionRecording();
}

void ActionMacroDialog::updateState(bool recording, bool playing, int eventCount)
{
    m_recordButton->setText(recording ? tr("Stop recording") : tr("Start recording"));
    m_recordButton->setEnabled(!playing);
    m_saveButton->setEnabled(!recording && !playing && eventCount > 0);
    m_loadButton->setEnabled(!recording && !playing);
    m_playButton->setEnabled(!recording && !playing && eventCount > 0);
    m_stopButton->setEnabled(recording || playing);
    m_repeatSpin->setEnabled(!recording && !playing);
    m_intervalSpin->setEnabled(!recording && !playing);

    if (recording) {
        m_statusLabel->setText(tr("Recording... %1 events captured.").arg(eventCount));
    } else if (playing) {
        m_statusLabel->setText(tr("Playing %1 events...").arg(eventCount));
    } else {
        m_statusLabel->setText(tr("Ready. %1 events in memory.").arg(eventCount));
    }
}

void ActionMacroDialog::updateProgress(int currentEvent, int totalEvents, int currentLoop, int totalLoops)
{
    const QString loopText = totalLoops == 0 ? tr("infinite") : QString::number(totalLoops);
    m_progressLabel->setText(tr("Loop %1/%2, event %3/%4")
                                 .arg(currentLoop).arg(loopText).arg(currentEvent).arg(totalEvents));
}

void ActionMacroDialog::showError(const QString &message)
{
    QMessageBox::warning(this, tr("Action Macro"), message);
}
