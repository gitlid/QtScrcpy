#include "actionmacrodialog.h"
#include "actionmacrohotkey.h"

#include <QCloseEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

ActionMacroDialog::ActionMacroDialog(const QString &serial, QWidget *parent)
    : QDialog(parent), m_serial(serial)
{
    setWindowTitle(tr("Action Macro 0.1.0-rc.1 - %1").arg(serial));
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(520, 360);

    m_statusLabel = new QLabel(this);
    m_progressLabel = new QLabel(tr("No playback in progress."), this);
    m_errorLabel = new QLabel(this);
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setTextFormat(Qt::PlainText);
    m_errorLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_recordButton = new QPushButton(tr("Start recording"), this);
    m_saveButton = new QPushButton(tr("Save..."), this);
    m_loadButton = new QPushButton(tr("Load..."), this);
    m_playButton = new QPushButton(tr("Play"), this);
    m_stopButton = new QPushButton(tr("Emergency stop"), this);
    m_stopButton->setToolTip(tr("Stop this device, cancel pending mapped input, and release held keys and touches."));
    m_repeatSpin = new QSpinBox(this);
    m_repeatSpin->setRange(0, 9999);
    m_repeatSpin->setValue(1);
    m_repeatSpin->setSpecialValueText(tr("Until stopped"));
    m_intervalSpin = new QSpinBox(this);
    m_intervalSpin->setRange(0, 600000);
    m_intervalSpin->setValue(500);
    m_intervalSpin->setSuffix(tr(" ms"));

    auto *hotkey = ActionMacroHotkey::instance();
    auto *hotkeyLabel = new QLabel(hotkey->globalAvailable()
        ? tr("Ctrl+Shift+X: Windows global emergency stop (all connected macro windows).")
        : tr("Ctrl+Shift+X: application-only stop. Global registration is unavailable; keep QtScrcpy focused or use Emergency stop."), this);
    hotkeyLabel->setWordWrap(true);
    auto *hint = new QLabel(tr("Restore the starting page before playback. Keep the same device, display size and orientation.\n"
                              "Macro files may contain typed or clipboard text. Do not record passwords or share sensitive macros."), this);
    hint->setWordWrap(true);

    auto *fileButtons = new QHBoxLayout;
    fileButtons->addWidget(m_recordButton);
    fileButtons->addWidget(m_saveButton);
    fileButtons->addWidget(m_loadButton);
    auto *options = new QFormLayout;
    options->addRow(tr("Repeat count:"), m_repeatSpin);
    options->addRow(tr("Loop interval:"), m_intervalSpin);
    auto *playButtons = new QHBoxLayout;
    playButtons->addWidget(m_playButton);
    playButtons->addWidget(m_stopButton);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_statusLabel);
    layout->addLayout(fileButtons);
    layout->addLayout(options);
    layout->addLayout(playButtons);
    layout->addWidget(m_progressLabel);
    layout->addWidget(m_errorLabel);
    layout->addWidget(hotkeyLabel);
    layout->addWidget(hint);

    connect(m_recordButton, &QPushButton::clicked, this, &ActionMacroDialog::toggleRecording);
    connect(m_saveButton, &QPushButton::clicked, this, &ActionMacroDialog::saveMacro);
    connect(m_loadButton, &QPushButton::clicked, this, &ActionMacroDialog::loadMacro);
    connect(m_playButton, &QPushButton::clicked, this, &ActionMacroDialog::playMacro);
    connect(m_stopButton, &QPushButton::clicked, this, &ActionMacroDialog::emergencyStop);
    connect(qApp, &QCoreApplication::aboutToQuit, this, &ActionMacroDialog::emergencyStop);

    // Bind this dialog to this exact session. A stale dialog must never send
    // actions to a new device object that happens to reuse the same serial.
    m_device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    m_connected = !m_device.isNull();
    if (m_device) {
        hotkey->watch(m_device.data());
        connect(m_device.data(), &qsc::IDevice::actionMacroStateChanged, this, &ActionMacroDialog::updateState);
        connect(m_device.data(), &qsc::IDevice::actionMacroProgress, this, &ActionMacroDialog::updateProgress);
        connect(m_device.data(), &qsc::IDevice::actionMacroError, this, &ActionMacroDialog::showError);
        connect(m_device.data(), &qsc::IDevice::deviceDisconnected, this,
                [this](const QString &) { markDisconnected(); });
        connect(m_device.data(), &QObject::destroyed, this, [this]() { markDisconnected(); });
    }
    refreshFromDevice();
}

ActionMacroDialog::~ActionMacroDialog()
{
    emergencyStop();
}

QPointer<qsc::IDevice> ActionMacroDialog::device() const
{
    return m_connected ? m_device : QPointer<qsc::IDevice>();
}

void ActionMacroDialog::markDisconnected()
{
    m_connected = false;
    updateState(false, false, 0);
    m_statusLabel->setText(tr("Device disconnected. Reconnect and open a new macro window."));
    m_progressLabel->setText(tr("Playback stopped; no automatic resume."));
}

void ActionMacroDialog::refreshFromDevice()
{
    const auto current = device();
    if (!current) { markDisconnected(); return; }
    updateState(current->isActionRecording(), current->isActionPlaying(), current->actionMacroEventCount());
}

bool ActionMacroDialog::confirmDiscard()
{
    if (!m_dirty) { return true; }
    const auto answer = QMessageBox::question(this, tr("Unsaved action macro"),
        tr("Save the current recording before continuing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) { return false; }
    if (answer == QMessageBox::Save) {
        saveMacro();
        return !m_dirty;
    }
    return true;
}

void ActionMacroDialog::toggleRecording()
{
    auto current = device();
    if (!current) { showError(tr("The device is no longer connected.")); return; }
    if (current->isActionRecording()) { current->stopActionRecording(); return; }
    if (!confirmDiscard()) { return; }
    current = device(); // The save dialog may have outlived the connection.
    if (!current) { return; }
    m_lastError.clear();
    m_errorLabel->clear();
    if (!current->startActionRecording()) {
        if (m_lastError.isEmpty()) { showError(tr("Recording could not start. Wait for the video and stop playback first.")); }
        return;
    }
    m_currentFile.clear();
    m_dirty = true;
    m_progressLabel->setText(tr("Record in the video window. Click Stop recording when finished."));
}

void ActionMacroDialog::saveMacro()
{
    auto current = device();
    if (!current) { showError(tr("The device is no longer connected.")); return; }
    const QString suggested = m_currentFile.isEmpty() ? QString("action-macro.qsmacro.json") : m_currentFile;
    const QString path = QFileDialog::getSaveFileName(this, tr("Save action macro"), suggested,
        tr("QtScrcpy action macro (*.qsmacro.json);;JSON files (*.json)"));
    if (path.isEmpty()) { return; }
    current = device();
    if (!current) { showError(tr("Device disconnected before saving.")); return; }
    QString error;
    if (!current->saveActionMacro(path, &error)) { showError(error); return; }
    m_currentFile = path;
    m_dirty = false;
    m_statusLabel->setText(tr("Saved %1 events to %2").arg(current->actionMacroEventCount()).arg(path));
    m_errorLabel->clear();
}

void ActionMacroDialog::loadMacro()
{
    if (!device()) { showError(tr("The device is no longer connected.")); return; }
    if (!confirmDiscard()) { return; }
    const QString path = QFileDialog::getOpenFileName(this, tr("Load action macro"), m_currentFile,
        tr("QtScrcpy action macro (*.qsmacro.json);;JSON files (*.json)"));
    if (path.isEmpty()) { return; }
    const auto current = device();
    if (!current) { showError(tr("Device disconnected before loading.")); return; }
    QString error;
    if (!current->loadActionMacro(path, &error)) { showError(error); return; }
    m_currentFile = path;
    m_dirty = false;
    m_errorLabel->clear();
    m_progressLabel->setText(tr("Loaded %1 events. Restore the starting page before Play.").arg(current->actionMacroEventCount()));
}

void ActionMacroDialog::playMacro()
{
    if (m_repeatSpin->value() == 0 && QMessageBox::question(this, tr("Continuous playback"),
        tr("Run until stopped? Confirm that this is a safe test screen and that the emergency stop is accessible."),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) { return; }
    const auto current = device();
    if (!current) { showError(tr("The device is no longer connected.")); return; }
    m_lastError.clear();
    m_errorLabel->clear();
    if (!current->playActionMacro(m_repeatSpin->value(), m_intervalSpin->value()) && m_lastError.isEmpty()) {
        showError(tr("Playback could not start. Load or record a macro, wait for video, and stop recording first."));
    }
}

void ActionMacroDialog::emergencyStop()
{
    auto current = m_device;
    if (current) { current->stopActionPlayback(); }
    if (current) { current->stopActionRecording(); }
}

void ActionMacroDialog::closeEvent(QCloseEvent *event)
{
    // Stop before any modal save prompt; even Cancel never resumes playback.
    emergencyStop();
    if (!confirmDiscard()) { event->ignore(); return; }
    event->accept();
}

void ActionMacroDialog::reject()
{
    close(); // Escape follows the same safe-stop and save-confirmation path.
}

void ActionMacroDialog::updateState(bool recording, bool playing, int eventCount)
{
    const bool available = m_connected && !m_device.isNull();
    if (recording) { m_dirty = true; }
    m_recordButton->setText(recording ? tr("Stop recording") : tr("Start recording"));
    m_recordButton->setEnabled(available && !playing);
    m_saveButton->setEnabled(available && !recording && !playing && eventCount > 0);
    m_loadButton->setEnabled(available && !recording && !playing);
    m_playButton->setEnabled(available && !recording && !playing && eventCount > 0);
    m_stopButton->setEnabled(available && (recording || playing));
    m_repeatSpin->setEnabled(available && !recording && !playing);
    m_intervalSpin->setEnabled(available && !recording && !playing);
    if (!available) { m_statusLabel->setText(tr("Device disconnected.")); }
    else if (recording) { m_statusLabel->setText(tr("Recording... %1 events captured.").arg(eventCount)); }
    else if (playing) { m_statusLabel->setText(tr("Playing %1 events...").arg(eventCount)); }
    else { m_statusLabel->setText(tr("Ready. %1 events in memory.%2").arg(eventCount).arg(m_dirty ? tr(" Unsaved changes.") : QString())); }
}

void ActionMacroDialog::updateProgress(int currentEvent, int totalEvents, int currentLoop, int totalLoops)
{
    const QString loopText = totalLoops == 0 ? tr("infinite") : QString::number(totalLoops);
    m_progressLabel->setText(tr("Loop %1/%2, event %3/%4").arg(currentLoop).arg(loopText).arg(currentEvent).arg(totalEvents));
}

void ActionMacroDialog::showError(const QString &message)
{
    // Non-modal: an error must not run a nested event loop or hide Stop.
    m_lastError = message;
    m_errorLabel->setText(tr("Error: %1").arg(message));
}
