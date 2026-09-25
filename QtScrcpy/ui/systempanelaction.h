#ifndef SYSTEMPANELACTION_H
#define SYSTEMPANELACTION_H
#include "appsession.h"
#include <functional>

// Resolve OEM panels before swiping. A BACK is only sent for a positively
// identified system overlay, never for an application or an unreadable focus.
class SystemPanelAction : public QObject {
    Q_OBJECT
public:
    SystemPanelAction(qsc::IDevice *device, QObject *parent = nullptr, AppCommands *commands = nullptr);
    ~SystemPanelAction() override { cancel(); }
    void request(bool settings);
    void cancel();
    void setBlocked(std::function<bool()> blocked) { m_blocked = std::move(blocked); }
    static bool expandedPanel(const QString &focus);
signals:
    void failure(const QString &message);
private:
    bool available() const;
    void probe();
    QPointer<qsc::IDevice> m_device;
    AppCommands *m_commands;
    std::function<bool()> m_blocked;
    QString m_tag;
    int m_generation = 0, m_check = 0;
    bool m_settings = false, m_pending = false, m_sentBack = false;
    bool m_waitedForDismissal = false;
};
#endif
