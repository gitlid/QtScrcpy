#ifndef DEVICEROTATIONMENU_H
#define DEVICEROTATIONMENU_H
#include "devicerotation.h"
#include <QMenu>
#include <QPointer>
class QProgressDialog;

// Separate UI adapter keeps the command/state machine testable without a phone.
class DeviceRotationMenu : public QMenu {
    Q_OBJECT
public:
    DeviceRotationMenu(qsc::IDevice *device, AppSession *apps, QWidget *parent = nullptr,
                       DeviceRotation *rotation = nullptr);
    void choose(DeviceRotation::Mode mode);
private:
    bool idleInput() const;
    QPointer<qsc::IDevice> m_device;
    QPointer<AppSession> m_apps;
    DeviceRotation *m_rotation;
    QPointer<QProgressDialog> m_progress;
    bool m_connected = true;
};
#endif
