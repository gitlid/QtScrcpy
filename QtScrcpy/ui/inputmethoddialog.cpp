#include "inputmethoddialog.h"
#include "appsession.h"
#include "appcommandprocess.h"
#include <QInputMethodEvent>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>

void ImeComposeEdit::inputMethodEvent(QInputMethodEvent *event)
{
    m_composing = !event->preeditString().isEmpty();
    QPlainTextEdit::inputMethodEvent(event);
    emit compositionChanged();
}

InputMethodDialog::InputMethodDialog(qsc::IDevice *device, AppSession *apps, QWidget *parent, AppCommands *commands)
    : QDialog(parent), m_device(device), m_apps(apps),
      m_commands(commands ? commands : new AdbAppCommands(this))
{
    setWindowTitle(tr("输入法与中文输入"));
    setObjectName("inputMethodDialog");
    resize(600, 400);
    setWindowModality(Qt::NonModal);
    m_connected = device && !device->isCameraMode();
    auto *layout = new QVBoxLayout(this);
    auto *phoneHint = new QLabel(tr("手机拼音：先在手机启用中文输入法，电脑保持英文。下列按钮直接发送实体键盘组合键，\n不受游戏键位映射影响；实际切换方式由手机输入法决定。"), this);
    phoneHint->setWordWrap(true); layout->addWidget(phoneHint);
    auto *row = new QHBoxLayout;
    const QStringList captions{tr("手机中/英（Shift）"), tr("手机切换（Ctrl+空格）"), tr("手机切换（Shift+空格）")};
    for (int i = 0; i < captions.size(); ++i) {
        auto *button = new QPushButton(captions[i], this);
        button->setObjectName(QString("imeShortcut%1").arg(i));
        button->setAutoDefault(false); button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QPushButton::clicked, this, [this, i] { shortcut(i); });
        row->addWidget(button); m_shortcuts.append(button);
    }
    layout->addLayout(row);
    auto *settingsRow = new QHBoxLayout;
    for (int i = 0; i < 2; ++i) {
        auto *button = new QPushButton(i ? tr("手机实体键盘设置") : tr("手机输入法设置"), this);
        button->setObjectName(QString("imeSettings%1").arg(i));
        button->setAutoDefault(false); button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QPushButton::clicked, this, [this, i] { openSettings(i != 0); });
        settingsRow->addWidget(button); m_settings.append(button);
    }
    layout->addLayout(settingsRow);
    auto *hostHint = new QLabel(tr("电脑拼音：先点击手机的目标输入框，再在下框切换 Windows 中文拼音并选词。\n点击“发送已选文字”后，以手机剪贴板粘贴发送一次；不会把拼音字母同时当作实体按键。\n此操作会替换手机剪贴板；不是所有应用都允许粘贴。请勿在密码或支付页面使用。"), this);
    hostHint->setWordWrap(true); layout->addWidget(hostHint);
    m_edit = new ImeComposeEdit(this);
    m_edit->setObjectName("imeText");
    m_edit->setAttribute(Qt::WA_InputMethodEnabled, true);
    m_edit->setPlaceholderText(tr("在这里输入并选定中文，例如：你好。Enter 只换行，不会自动发送。"));
    layout->addWidget(m_edit, 1);
    m_send = new QPushButton(tr("发送已选文字"), this);
    m_send->setObjectName("sendImeText"); m_send->setAutoDefault(false); m_send->setDefault(false);
    m_send->setFocusPolicy(Qt::NoFocus); // Do not cancel a live candidate by stealing editor focus.
    layout->addWidget(m_send);
    m_status = new QLabel(tr("实体键盘设备保持连接。手机端与电脑端输入法的中/英状态不会自动同步。"), this);
    m_status->setObjectName("imeStatus"); m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText);
    layout->addWidget(m_status);
    connect(m_send, &QPushButton::clicked, this, &InputMethodDialog::sendText);
    connect(m_edit, &QPlainTextEdit::textChanged, this, &InputMethodDialog::refresh);
    connect(m_edit, &ImeComposeEdit::compositionChanged, this, &InputMethodDialog::refresh);
    connect(m_commands, &AppCommands::finished, this,
            [this](const QString &tag, bool ok, const QString &output, const QString &error) {
        if (tag != "ime-settings" || !m_connected) return;
        const bool failed = !ok || output.contains("Error:") || output.contains("Exception") || error.contains("Exception");
        m_status->setText(failed ? tr("手机未能打开设置，请在手机上手动进入语言和输入法设置。")
                               : tr("已请求打开手机设置；完成后请先回到目标应用并点击输入框。"));
    });
    if (device) {
        connect(device, &qsc::IDevice::actionMacroStateChanged, this, [this](bool, bool, int) { refresh(); });
        connect(device, &qsc::IDevice::deviceDisconnected, this, [this](const QString &) {
            m_connected = false; m_commands->cancelAll(); refresh();
        });
        connect(device, &QObject::destroyed, this, [this] { m_connected = false; m_commands->cancelAll(); refresh(); });
        if (available()) { device->prepareKeymapEditing(); device->releaseKeyboard(); }
    }
    if (apps) connect(apps, &AppSession::lockChanged, this, &InputMethodDialog::refresh);
    refresh();
    QTimer::singleShot(0, m_edit, [this] { m_edit->setFocus(Qt::OtherFocusReason); });
}
InputMethodDialog::~InputMethodDialog() { m_commands->cancelAll(); }
bool InputMethodDialog::available() const
{
    return m_connected && m_device && !m_device->isCameraMode() && !m_device->isActionPlaying()
        && !m_device->isActionPaused() && !m_device->isActionRecording() && (!m_apps || !m_apps->locked());
}
void InputMethodDialog::refresh()
{
    const bool ready = available();
    m_send->setEnabled(ready && !m_edit->composing() && !m_edit->toPlainText().isEmpty());
    for (auto *button : m_shortcuts) button->setEnabled(ready && m_device->isUhidKeyboardEnabled());
    for (auto *button : m_settings) button->setEnabled(ready);
    if (!ready) {
        m_commands->cancelAll();
        m_status->setText(!m_connected || !m_device ? tr("设备已断开，未发送的文字保留在本窗口；不会发送到重连后的其他会话。")
                                                  : tr("预制操作或录制占用输入，请先停止。仅暂停仍不能发送文字或切换输入法。"));
    }
}
void InputMethodDialog::sendText()
{
    if (!available() || m_edit->composing()) { refresh(); return; }
    const QString text = m_edit->toPlainText();
    if (text.isEmpty()) return;
    if (text.toUtf8().size() > 65536 || text.contains(QChar(0))) {
        m_status->setText(tr("文字包含无效字符或超过 64 KiB，未发送。")); return;
    }
    if (!m_device->pasteImeText(text)) {
        m_status->setText(tr("未能提交文字，内容已保留。请检查连接及当前是否有操作占用输入。")); return;
    }
    m_edit->clear();
    m_status->setText(tr("粘贴请求已发送一次，请在手机确认。没有应用插入回执；不会自动重试或按下发送键。"));
    m_edit->setFocus(Qt::OtherFocusReason);
}
void InputMethodDialog::shortcut(int which)
{
    if (!available()) { refresh(); return; }
    const bool ok = m_device->sendImeShortcut(which);
    m_status->setText(ok ? tr("实体键盘组合键已发送。请查看手机的中/英或输入法状态，点击投屏后继续输入。")
                        : tr("切换键未发送，请启用 UHID 并检查连接。"));
}
void InputMethodDialog::openSettings(bool physical)
{
    if (!available()) { refresh(); return; }
    m_device->releaseKeyboard();
    m_commands->run("ime-settings", m_device->getSerial(), {"shell", "am", "start", "-a",
        physical ? "android.settings.HARD_KEYBOARD_SETTINGS" : "android.settings.INPUT_METHOD_SETTINGS"}, 5000);
}
