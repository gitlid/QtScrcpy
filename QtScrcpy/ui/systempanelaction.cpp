#include "systempanelaction.h"
#include "appcommandprocess.h"
#include <QRegularExpression>

SystemPanelAction::SystemPanelAction(qsc::IDevice *device, QObject *parent, AppCommands *commands)
    : QObject(parent), m_device(device), m_commands(commands ? commands : new AdbAppCommands(this)) {
    connect(m_commands, &AppCommands::finished, this,
            [this](const QString &tag, bool ok, const QString &output, const QString &) {
        if (!m_pending || tag != m_tag) return;
        if (!available()) { cancel(); return; }
        if (!ok || !output.contains("mCurrentFocus=")) {
            cancel(); emit failure(tr("无法确认手机前台面板，请检查连接后重试。")); return;
        }
        if (expandedPanel(output)) {
            if (!m_sentBack) { m_sentBack = true; m_device->postGoBack(); }
            if (++m_check > 5) { cancel(); emit failure(tr("手机面板未收起，请手动收起后重试。")); return; }
            const int generation = m_generation;
            QTimer::singleShot(200, this, [this, generation] { if (m_pending && generation == m_generation) probe(); });
            return;
        }
        if (m_sentBack && !m_waitedForDismissal) {
            // Window focus changes before vivo finishes its shade animation.
            // Recheck after settling; otherwise the next DOWN is swallowed.
            m_waitedForDismissal = true;
            ++m_check;
            const int generation = m_generation;
            QTimer::singleShot(500, this, [this, generation] { if (m_pending && generation == m_generation) probe(); });
            return;
        }
        const bool settings = m_settings;
        m_pending = false; m_tag.clear();
        if (settings) m_device->expandSettingsPanel(); else m_device->expandNotificationPanel();
    });
    if (device) {
        connect(device, &qsc::IDevice::deviceDisconnected, this, [this](const QString &) { cancel(); m_device.clear(); });
        connect(device, &QObject::destroyed, this, &SystemPanelAction::cancel);
        connect(device, &qsc::IDevice::actionMacroStateChanged, this, [this](bool, bool, int) { if (!available()) cancel(); });
    }
}
bool SystemPanelAction::expandedPanel(const QString &focus) {
    static const QRegularExpression pattern(QStringLiteral("mCurrentFocus=Window\\{[^\\r\\n}]*\\bu\\d+\\s+(?:UpSlideTransparentView|NotificationShade)\\s*\\}"));
    return pattern.match(focus).hasMatch();
}
bool SystemPanelAction::available() const {
    return m_device && !m_device->isCameraMode() && !m_device->isActionPlaying() && !m_device->isActionPaused()
        && (!m_blocked || !m_blocked());
}
void SystemPanelAction::request(bool settings) {
    cancel();
    if (!available()) { emit failure(tr("预制操作占用输入，请先停止后展开系统面板。")); return; }
    m_settings = settings; m_pending = true; m_sentBack = false; m_waitedForDismissal = false; m_check = 0;
    probe();
}
void SystemPanelAction::probe() {
    if (!available()) { cancel(); return; }
    m_tag = QString("panel-focus-%1-%2").arg(m_generation).arg(m_check);
    m_commands->run(m_tag, m_device->getSerial(), {"shell", "dumpsys window | grep mCurrentFocus"}, 2500);
}
void SystemPanelAction::cancel() {
    ++m_generation; m_pending = false; m_tag.clear(); m_commands->cancelAll();
}
