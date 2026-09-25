#include "devicerotationmenu.h"
#include <QMessageBox>
#include <QActionGroup>
#include <QProgressDialog>

DeviceRotationMenu::DeviceRotationMenu(qsc::IDevice *device, AppSession *apps, QWidget *parent, DeviceRotation *rotation)
    : QMenu(parent), m_device(device), m_apps(apps),
      m_rotation(rotation ? rotation : new DeviceRotation(device ? device->getSerial() : QString(), this)) {
    setObjectName("deviceRotationMenu");
    m_connected = device && !device->isCameraMode() && !device->isFlexDisplay();
    const QStringList labels{tr("切换横竖屏"),tr("固定横屏"),tr("固定竖屏"),tr("恢复原设置")};
    for (int i=0;i<labels.size();++i) {
        if (i==3) addSeparator();
        auto *action=addAction(labels[i]); action->setData(i);
        action->setObjectName(QString("rotationMode%1").arg(i));
        connect(action,&QAction::triggered,this,[this,i]{choose(static_cast<DeviceRotation::Mode>(i));});
    }
    m_rotation->setWriteGuard([this]{return idleInput();});
    connect(this,&QMenu::aboutToShow,this,[this]{
        for(auto *action:actions()) action->setEnabled(m_connected && m_device && !m_rotation->busy());
    });
    connect(m_rotation,&DeviceRotation::busyChanged,this,[this](bool busy){
        if (busy) {
            m_progress=new QProgressDialog(tr("正在读取、备份并设置手机主屏方向…"),QString(),0,0,parentWidget());
            m_progress->setWindowTitle(tr("设备旋转")); m_progress->setCancelButton(nullptr);
            m_progress->setWindowModality(Qt::ApplicationModal); m_progress->setMinimumDuration(0);
            m_progress->show();
        } else if(m_progress) { m_progress->close(); m_progress->deleteLater(); m_progress=nullptr; }
    });
    connect(m_rotation,&DeviceRotation::confirmationRequired,this,[this](const QString &text){
        if(!m_connected || !m_device) { m_rotation->confirm(false); return; }
        QWidget *owner=m_progress ? static_cast<QWidget *>(m_progress.data()) : parentWidget();
        const auto answer=QMessageBox::question(owner,tr("确认设备旋转"),text,QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel);
        m_rotation->confirm(answer==QMessageBox::Yes && idleInput());
    });
    connect(m_rotation,&DeviceRotation::finished,this,[this](bool ok,const QString &message){
        if(!m_connected) return;
        if(ok) QMessageBox::information(parentWidget(),tr("设备旋转"),message);
        else QMessageBox::warning(parentWidget(),tr("设备旋转"),message + tr("\n手机不支持时，可改用“仅旋转投屏画面”，不改变手机方向。"));
    });
    if(device) {
        auto disconnected=[this]{ m_connected=false; m_rotation->disconnectDevice(); close(); };
        connect(device,&qsc::IDevice::deviceDisconnected,this,[disconnected](const QString &){disconnected();});
        connect(device,&QObject::destroyed,this,disconnected);
    }
}
bool DeviceRotationMenu::idleInput() const {
    return m_connected && m_device && !m_device->isActionPlaying() && !m_device->isActionRecording()
        && (!m_apps || (!m_apps->locked() && !m_apps->closingApp()));
}
void DeviceRotationMenu::choose(DeviceRotation::Mode mode) {
    if(!m_connected || !m_device || m_rotation->busy() || (m_apps && m_apps->closingApp())) return;
    if(!idleInput()) {
        const auto answer=QMessageBox::question(parentWidget(),tr("旋转前停止预制操作"),
            tr("方向改变会影响录制坐标。是否先停止当前预制操作/录制及自动切回，再执行旋转？\n仅暂停仍由宏占用输入；取消则不改变方向。"),
            QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel);
        if(answer!=QMessageBox::Yes || !m_connected || !m_device) return;
        if(m_apps) m_apps->stopMacro();
        m_device->stopActionPlayback(); m_device->stopActionRecording();
    }
    if(!idleInput()) return;
    m_device->prepareKeymapEditing(); m_device->releaseKeyboard();
    m_rotation->request(mode);
}

void DeviceRotationMenu::addViewRotation(std::function<int()> current, std::function<void(int)> apply,
        std::function<ViewOrientation::Mode()> currentMode,
        std::function<void(ViewOrientation::Mode)> applyMode) {
    if (!current || !apply) return;
    addSeparator();
    auto *menu = addMenu(tr("仅旋转投屏画面（不改变手机）"));
    menu->setObjectName("viewRotationMenu");
    if (currentMode && applyMode) {
        auto *group = new QActionGroup(menu); group->setExclusive(true);
        const QStringList modes{tr("跟随手机方向（取消保持）"), tr("保持横屏展示（自动适配）"), tr("保持竖屏展示（自动适配）")};
        const QStringList names{"viewFollowPhone", "viewKeepLandscape", "viewKeepPortrait"};
        for (int i = 0; i < modes.size(); ++i) {
            auto *action = menu->addAction(modes[i]); action->setCheckable(true);
            action->setObjectName(names[i]); action->setData(i); group->addAction(action);
            connect(action, &QAction::triggered, this, [this, applyMode, currentMode, group, i] {
                const QPointer<DeviceRotationMenu> guard(this);
                if (m_connected && m_device && !m_rotation->busy()) applyMode(static_cast<ViewOrientation::Mode>(i));
                if (!guard) return;
                for (auto *item : group->actions()) item->setChecked(item->data().toInt() == int(currentMode()));
            });
        }
        connect(menu, &QMenu::aboutToShow, this, [currentMode, group] {
            for (auto *item : group->actions()) item->setChecked(item->data().toInt() == int(currentMode()));
        });
        menu->addSeparator();
    }
    const QStringList labels{tr("顺时针 90°"), tr("逆时针 90°"), tr("旋转 180°"), tr("恢复画面方向")};
    const QList<int> deltas{1, -1, 2, 0};
    for (int i = 0; i < labels.size(); ++i) {
        auto *action = menu->addAction(labels[i]); action->setObjectName(QString("viewRotation%1").arg(i));
        const int delta = deltas[i];
        action->setToolTip(delta ? tr("手动旋转后保持选中的横/竖屏；手机改变方向时自动补偿，不反复调整窗口。")
                                : tr("清除电脑端展示锁定和旋转，重新跟随手机方向。"));
        connect(action, &QAction::triggered, this, [this, current, apply, applyMode, delta] {
            if (!m_connected || !m_device || m_rotation->busy()) return;
            if (!delta && applyMode) applyMode(ViewOrientation::FollowPhone);
            else apply(delta ? current() + delta : 0);
        });
    }
}
