from pathlib import Path
root=Path(__file__).resolve().parents[1]
def edit(p,o,n):
 f=root/p;t=f.read_text(encoding='utf-8-sig');assert t.count(o)==1,(p,o[:60],t.count(o));f.write_text(t.replace(o,n),encoding='utf8')
if '0.2.0-rc.1' not in (root/'QtScrcpy/ui/actionmacrodialog.cpp').read_text():
 p='QtScrcpy/ui/actionmacrodialog.h'
 edit(p,'class QSpinBox;','class QSpinBox;\nclass MacroExecutionOptions;\nclass QTimer;')
 edit(p,'    void playMacro();','    void playMacro();\n    void togglePause();')
 edit(p,'    QSpinBox *m_repeatSpin = Q_NULLPTR;\n    QSpinBox *m_intervalSpin = Q_NULLPTR;','''    MacroExecutionOptions *m_options = Q_NULLPTR;
    QPushButton *m_pauseButton = Q_NULLPTR;
    QLabel *m_elapsedLabel = Q_NULLPTR;''')
 p='QtScrcpy/ui/actionmacrodialog.cpp'
 edit(p,'#include "actionmacrohotkey.h"','#include "actionmacrohotkey.h"\n#include "macroexecutionoptions.h"\n#include <QTimer>')
 edit(p,'Action Macro 0.1.0-rc.1','Action Macro 0.2.0-rc.1')
 f=root/p;t=f.read_text();a=t.index('    m_repeatSpin = new QSpinBox(this);');b=t.index('\n    auto *hotkey',a)
 t=t[:a]+'''    m_options = new MacroExecutionOptions(this);
    m_pauseButton = new QPushButton(tr("暂停"), this);
    m_pauseButton->setObjectName("pauseActionMacro");
    m_elapsedLabel = new QLabel(this);
    auto *elapsedTimer = new QTimer(this);
    elapsedTimer->setInterval(100);
    connect(elapsedTimer, &QTimer::timeout, this, [this]() {
        const auto d = device();
        if (d) { m_elapsedLabel->setText(tr("有效运行：%1 秒（包含循环间隔，不包含暂停）").arg(d->actionMacroElapsedMs() / 1000.0, 0, 'f', 1)); }
    });
    elapsedTimer->start();
''' +t[b:];f.write_text(t)
 edit(p,'    auto *options = new QFormLayout;\n    options->addRow(tr("Repeat count:"), m_repeatSpin);\n    options->addRow(tr("Loop interval:"), m_intervalSpin);','')
 edit(p,'    playButtons->addWidget(m_stopButton);','    playButtons->addWidget(m_pauseButton);\n    playButtons->addWidget(m_stopButton);')
 edit(p,'    layout->addLayout(options);','    layout->addWidget(m_options);')
 edit(p,'    layout->addWidget(m_progressLabel);','    layout->addWidget(m_progressLabel);\n    layout->addWidget(m_elapsedLabel);')
 edit(p,'    layout->addWidget(hint);','''    layout->addWidget(hint);
    auto *pauseHint = new QLabel(hotkey->pauseGlobalAvailable()
        ? tr("Ctrl+Shift+P：全局暂停。继续请点击本窗口按钮；中断长按/拖动时需要确认跳过当前动作组。")
        : tr("Ctrl+Shift+P：应用内暂停（全局注册失败）。继续请点击本窗口按钮。"), this);
    pauseHint->setWordWrap(true);layout->addWidget(pauseHint);
    auto *speedHint = new QLabel(tr("倍率仅缩放脚本时序，不会加速手机应用；循环间隔不缩放。8× 可能使长按或页面等待过短。"), this);
    speedHint->setWordWrap(true);layout->addWidget(speedHint);''')
 edit(p,'    connect(m_playButton, &QPushButton::clicked, this, &ActionMacroDialog::playMacro);','''    connect(m_playButton, &QPushButton::clicked, this, &ActionMacroDialog::playMacro);
    connect(m_pauseButton, &QPushButton::clicked, this, &ActionMacroDialog::togglePause);''')
 edit(p,'    m_currentFile = path;\n    m_dirty = false;\n    m_statusLabel->setText','''    if (!MacroExecutionOptions::saveTo(path, m_options->json())) {
        showError(tr("宏事件已保存，但执行设置写入失败。请重试保存。"));return;
    }
    m_currentFile = path;
    m_dirty = false;
    m_statusLabel->setText''')
 edit(p,'    m_progressLabel->setText(tr("Loaded %1 events. Restore the starting page before Play.").arg(current->actionMacroEventCount()));','''    m_progressLabel->setText(tr("Loaded %1 events. Restore the starting page before Play.").arg(current->actionMacroEventCount()));
    if (!m_options->loadFrom(path)) { showError(tr("执行设置无效，已恢复为单次 1×；宏事件仍可使用。")); }''')
 edit(p,'    if (m_repeatSpin->value() == 0 && QMessageBox::question','    if (m_options->repeats() == 0 && m_options->limitMs() == 0 && QMessageBox::question')
 edit(p,'current->playActionMacro(m_repeatSpin->value(), m_intervalSpin->value())','current->playActionMacroAdvanced(m_options->repeats(), m_options->intervalMs(), m_options->multiplier(), m_options->limitMs())')
 edit(p,'void ActionMacroDialog::emergencyStop()','''void ActionMacroDialog::togglePause()
{
    auto current = device();
    if (!current) { return; }
    if (!current->isActionPaused()) {
        if (!current->pauseActionMacro()) { showError(tr("当前没有可暂停的录制或回放。")); }
        return;
    }
    if (current->actionMacroInterruptedInput()) {
        const auto result = QMessageBox::question(this, tr("当前动作已中断"),
            tr("暂停已释放按键和触点。继续将跳过中断的动作组直到全部输入释放的边界，不会重新点击或恢复同一段拖拽。确认手机页面正确后继续？"),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (result != QMessageBox::Yes) { return; }
    }
    current = device();
    if (current && !current->resumeActionMacro()) { showError(tr("无法继续：请检查连接和显示尺寸。")); }
}

void ActionMacroDialog::emergencyStop()''')
 edit(p,'    m_repeatSpin->setEnabled(available && !recording && !playing);\n    m_intervalSpin->setEnabled(available && !recording && !playing);','''    m_options->setEnabled(available && !recording && !playing);
    const bool paused = available && m_device->isActionPaused();
    m_pauseButton->setEnabled(available && (recording || playing));
    m_pauseButton->setText(paused ? tr("继续") : tr("暂停"));''')
 edit(p,'    else if (recording) { m_statusLabel','''    else if (paused) { m_statusLabel->setText(recording ? tr("录制已暂停（暂停期间不记录）") : tr("回放已暂停（保留轮次与剩余等待）")); }
    else if (recording) { m_statusLabel''')
 p='QtScrcpy/ui/actionmacrohotkey.h'
 edit(p,'    bool globalAvailable() const { return m_globalAvailable; }','''    bool globalAvailable() const { return m_globalAvailable; }
    bool pauseGlobalAvailable() const { return m_pauseGlobalAvailable; }
    void pauseAll()
    {
        const auto devices = m_devices;
        for (const auto &device : devices) { if (device) { device->pauseActionMacro(); } }
    }''')
 edit(p,'        if (m_globalAvailable) { UnregisterHotKey(nullptr, kHotkeyId); }','''        if (m_globalAvailable) { UnregisterHotKey(nullptr, kHotkeyId); }
        if (m_pauseGlobalAvailable) { UnregisterHotKey(nullptr, kPauseHotkeyId); }''')
 edit(p,'        if (key->key() != Qt::Key_X || modifiers != (Qt::ControlModifier | Qt::ShiftModifier)) { return false; }\n        if (event->type() == QEvent::KeyPress && !key->isAutoRepeat()) { stopAll(); }','''        if ((key->key() != Qt::Key_X && key->key() != Qt::Key_P) || modifiers != (Qt::ControlModifier | Qt::ShiftModifier)) { return false; }
        if (event->type() == QEvent::KeyPress && !key->isAutoRepeat()) {
            if (key->key() == Qt::Key_P) { pauseAll(); } else { stopAll(); }
        }''')
 edit(p,'        if (msg && msg->message == WM_HOTKEY && msg->wParam == kHotkeyId) {','''        if (msg && msg->message == WM_HOTKEY && msg->wParam == kPauseHotkeyId) { pauseAll(); return true; }
        if (msg && msg->message == WM_HOTKEY && msg->wParam == kHotkeyId) {''')
 edit(p,"        m_globalAvailable = RegisterHotKey(nullptr, kHotkeyId, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'X') != 0;","        m_globalAvailable = RegisterHotKey(nullptr, kHotkeyId, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'X') != 0;\n        m_pauseGlobalAvailable = RegisterHotKey(nullptr, kPauseHotkeyId, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'P') != 0;")
 edit(p,'    enum { kHotkeyId = 0x514D };','    enum { kHotkeyId = 0x514D, kPauseHotkeyId = 0x514E };\n    bool m_pauseGlobalAvailable = false;')
if 'mappingSnapshot' not in (root/'QtScrcpy/ui/videoform.h').read_text():
 edit('QtScrcpy/ui/videoform.h','#include <QWidget>','#include <QWidget>\n#include <QPixmap>')
 edit('QtScrcpy/ui/videoform.h','    bool isHost();','    bool isHost();\n    QPixmap mappingSnapshot() const;')
 edit('QtScrcpy/ui/videoform.cpp','void VideoForm::showToolForm(bool show)','''QPixmap VideoForm::mappingSnapshot() const
{
    auto *view = videoWidget();
    return view ? view->grab() : QPixmap();
}

void VideoForm::showToolForm(bool show)''')
 edit('QtScrcpy/ui/toolform.h','    void on_actionMacroBtn_clicked();','    void on_actionMacroBtn_clicked();\n    void on_keymapEditorBtn_clicked();')
 edit('QtScrcpy/ui/toolform.cpp','#include "actionmacrodialog.h"','#include "actionmacrodialog.h"\n#include "keymapeditor.h"\n#include <QMessageBox>')
 edit('QtScrcpy/ui/toolform.cpp','    ui->actionMacroBtn->setVisible(!camera);','    ui->actionMacroBtn->setVisible(!camera);\n    ui->keymapEditorBtn->setVisible(!camera);')
 edit('QtScrcpy/ui/toolform.cpp','    IconHelper::Instance()->SetIcon(ui->actionMacroBtn, QChar(0xf144), 15);','    IconHelper::Instance()->SetIcon(ui->actionMacroBtn, QChar(0xf144), 15);\n    IconHelper::Instance()->SetIcon(ui->keymapEditorBtn, QChar(0xf11c), 15);')
 f=root/'QtScrcpy/ui/toolform.cpp';f.write_text(f.read_text()+'''
void ToolForm::on_keymapEditorBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    QPointer<VideoForm> video = qobject_cast<VideoForm*>(parent());
    if (!device || !video || device->isCameraMode()) { return; }
    if (device->isActionPlaying() || device->isActionRecording()) {
        QMessageBox::information(this, tr("按键设置"), tr("请先停止录制或回放，再编辑按键映射。"));return;
    }
    const QSize frameSize = video->frameSize();
    const QPixmap snapshot = video->mappingSnapshot();
    if (snapshot.isNull() || !frameSize.isValid()) { return; }
    device->prepareKeymapEditing();
    KeymapEditor editor(snapshot, device->currentKeymapScript());
    connect(device.data(), &qsc::IDevice::deviceDisconnected, &editor, [&editor](const QString &) { editor.done(QDialog::Rejected); });
    connect(device.data(), &QObject::destroyed, &editor, [&editor]() { editor.done(QDialog::Rejected); });
    if (editor.exec() == QDialog::Accepted && device && video && video->frameSize() == frameSize
        && !device->isActionPlaying() && !device->isActionRecording()) {
        device->updateScript(editor.script());
    }
}
''')
 f=root/'QtScrcpy/ui/toolform.ui';t=f.read_text();a=t.index('   <item>\n    <widget class="QPushButton" name="actionMacroBtn">');t=t[:a]+'''   <item>
    <widget class="QPushButton" name="keymapEditorBtn">
     <property name="toolTip"><string>可视化按键设置</string></property>
     <property name="text"><string/></property>
    </widget>
   </item>
'''+t[a:];f.write_text(t)
 edit('QtScrcpy/CMakeLists.txt','    ui/actionmacrodialog.h','    ui/keymapeditor.h\n    ui/keymapeditor.cpp\n    ui/keymapdocument.h\n    ui/macroexecutionoptions.h\n    ui/actionmacrodialog.h')
print('Macro UI and keymap editor integrated; audio remains unchanged.')
