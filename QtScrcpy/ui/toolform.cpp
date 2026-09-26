#include <QDebug>
#include <QHideEvent>
#include <QMouseEvent>
#include <QShowEvent>

#include "actionmacrodialog.h"
#include "devicerotationmenu.h"
#include <QShortcut>
#include <QSignalBlocker>
#include "iconhelper.h"
#include "toolform.h"
#include "ui_toolform.h"
#include "videoform.h"
#include "../groupcontroller/groupcontroller.h"

ToolForm::ToolForm(VideoForm *view, QWidget *parent) : QWidget(parent ? parent : view), ui(new Ui::ToolForm), m_view(view)
{
    ui->setupUi(this);
    setAttribute(Qt::WA_NoMousePropagation);
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    ui->verticalLayout->setContentsMargins(4, 6, 4, 6);
    ui->verticalLayout->setSpacing(4);
    ui->verticalSpacer->changeSize(0, 6, QSizePolicy::Minimum, QSizePolicy::Fixed);
    for (auto *button : findChildren<QPushButton *>()) {
        button->setFixedSize(44, 36);
        button->setFocusPolicy(Qt::NoFocus);
        button->setAutoDefault(false);
    }
    ui->expandNotifyBtn->setToolTip(tr("展开通知栏：从手机左上方向下滑动（Ctrl+N）"));
    ui->expandSettingsBtn->setToolTip(tr("展开设置面板：从手机右上方向下滑动（Ctrl+Alt+N）"));
    ui->phoneCursorBtn->setToolTip(tr("手机跟随光标：在手机屏幕显示与电脑鼠标位置对应的圆环"));
    connect(ui->phoneCursorBtn, &QPushButton::toggled, this, [this](bool enabled) {
        if (m_view) m_view->setPhoneCursorEnabled(enabled);
    });
    if (m_view) connect(m_view, &VideoForm::phoneCursorEnabledChanged, this, [this](bool enabled) {
        const QSignalBlocker blocker(ui->phoneCursorBtn);
        ui->phoneCursorBtn->setChecked(enabled);
        ui->phoneCursorBtn->setStyleSheet(enabled ? "color: #00e5ff" : "");
    });

    updateGroupControl();

    initStyle();
}

ToolForm::~ToolForm()
{
    delete ui;
}

void ToolForm::setSerial(const QString &serial)
{
    m_serial = serial;
    updateCameraMode();
    const bool cursorSupported = m_view && m_view->phoneCursorSupported();
    ui->phoneCursorBtn->setEnabled(cursorSupported);
    ui->phoneCursorBtn->setToolTip(cursorSupported
        ? tr("手机跟随光标：在手机屏幕显示与电脑鼠标位置对应的圆环")
        : tr("手机光标需要主屏幕完整投屏，且采集方向设为自动；支持仅旋转投屏画面。"));
    if (m_rotationMenu) return; // Bind to the original live device, not a later replacement.
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (device && !device->isCameraMode() && !device->isFlexDisplay()) {
        auto *video = m_view.data();
        m_rotationMenu = new DeviceRotationMenu(device, video ? video->appSession() : nullptr, this);
        if (video && video->viewRotationSupported()) {
            const QPointer<VideoForm> view = video;
            m_rotationMenu->addViewRotation([view] { return view ? view->viewRotation() : 0; },
                [view](int turns) { if (view) view->setViewRotation(turns); },
                [view] { return view ? view->viewOrientationMode() : ViewOrientation::FollowPhone; },
                [view](ViewOrientation::Mode mode) { if (view) view->setViewOrientationMode(mode); });
        }
        ui->rotateBtn->setMenu(m_rotationMenu);
        ui->rotateBtn->setToolTip(tr("旋转：手机横竖屏控制，或仅旋转投屏画面（不改变手机）"));
        // Reuse the existing shortcut, replacing its silent control-message path.
        // UHID mode still reserves Ctrl+R for Android through ShortcutOverride.
        if (video) for (auto *shortcut : video->findChildren<QShortcut *>(QString(), Qt::FindDirectChildrenOnly)) {
            if (shortcut->key() != QKeySequence("Ctrl+r")) continue;
            QObject::disconnect(shortcut, SIGNAL(activated()), video, nullptr);
            connect(shortcut, &QShortcut::activated, m_rotationMenu, [this] {
                if (m_rotationMenu) m_rotationMenu->choose(DeviceRotation::Toggle);
            });
        }
    }
}

bool ToolForm::isHost()
{
    return m_isHost;
}

void ToolForm::updateCameraMode()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    const bool camera = device && device->isCameraMode();

    ui->groupControlBtn->setVisible(!camera);
    ui->expandNotifyBtn->setVisible(!camera);
    ui->expandSettingsBtn->setVisible(!camera);
    ui->rotateBtn->setVisible(!camera);
    ui->touchBtn->setVisible(!camera);
    ui->phoneCursorBtn->setVisible(!camera);
    ui->openScreenBtn->setVisible(!camera);
    ui->closeScreenBtn->setVisible(!camera);
    ui->powerBtn->setVisible(!camera);
    ui->volumeUpBtn->setVisible(!camera);
    ui->volumeDownBtn->setVisible(!camera);
    ui->appSwitchBtn->setVisible(!camera);
    ui->menuBtn->setVisible(!camera);
    ui->homeBtn->setVisible(!camera);
    ui->returnBtn->setVisible(!camera);
    ui->clipboardBtn->setVisible(!camera);
    ui->actionMacroBtn->setVisible(!camera);
    ui->cameraTorchBtn->setVisible(camera);
    ui->cameraZoomOutBtn->setVisible(camera);
    ui->cameraZoomInBtn->setVisible(camera);
}

void ToolForm::initStyle()
{
    IconHelper::Instance()->SetIcon(ui->fullScreenBtn, QChar(0xf0b2), 15);
    IconHelper::Instance()->SetIcon(ui->menuBtn, QChar(0xf096), 15);
    IconHelper::Instance()->SetIcon(ui->homeBtn, QChar(0xf1db), 15);
    //IconHelper::Instance()->SetIcon(ui->returnBtn, QChar(0xf104), 15);
    IconHelper::Instance()->SetIcon(ui->returnBtn, QChar(0xf053), 15);
    IconHelper::Instance()->SetIcon(ui->appSwitchBtn, QChar(0xf24d), 15);
    IconHelper::Instance()->SetIcon(ui->volumeUpBtn, QChar(0xf028), 15);
    IconHelper::Instance()->SetIcon(ui->volumeDownBtn, QChar(0xf027), 15);
    IconHelper::Instance()->SetIcon(ui->openScreenBtn, QChar(0xf06e), 15);
    IconHelper::Instance()->SetIcon(ui->closeScreenBtn, QChar(0xf070), 15);
    IconHelper::Instance()->SetIcon(ui->powerBtn, QChar(0xf011), 15);
    IconHelper::Instance()->SetIcon(ui->expandNotifyBtn, QChar(0xf103), 15);
    IconHelper::Instance()->SetIcon(ui->expandSettingsBtn, QChar(0xf013), 15);
    IconHelper::Instance()->SetIcon(ui->rotateBtn, QChar(0xf021), 15);
    IconHelper::Instance()->SetIcon(ui->screenShotBtn, QChar(0xf0c4), 15);
    IconHelper::Instance()->SetIcon(ui->touchBtn, QChar(0xf111), 15);
    IconHelper::Instance()->SetIcon(ui->phoneCursorBtn, QChar(0xf245), 15);
    IconHelper::Instance()->SetIcon(ui->groupControlBtn, QChar(0xf0c0), 15);
    IconHelper::Instance()->SetIcon(ui->clipboardBtn, QChar(0xf0c5), 15);
    IconHelper::Instance()->SetIcon(ui->actionMacroBtn, QChar(0xf144), 15);
    IconHelper::Instance()->SetIcon(ui->cameraTorchBtn, QChar(0xf0eb), 15);
    IconHelper::Instance()->SetIcon(ui->cameraZoomOutBtn, QChar(0xf010), 15);
    IconHelper::Instance()->SetIcon(ui->cameraZoomInBtn, QChar(0xf00e), 15);
}

void ToolForm::updateGroupControl()
{
    if (m_isHost) {
        ui->groupControlBtn->setStyleSheet("color: red");
    } else {
        ui->groupControlBtn->setStyleSheet("color: green");
    }

    GroupController::instance().updateDeviceState(m_serial);
}

void ToolForm::on_fullScreenBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }

    if (m_view) m_view->switchFullScreen();
}

void ToolForm::on_returnBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }
    device->postGoBack();
}

void ToolForm::on_homeBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }
    device->postGoHome();
}

void ToolForm::on_menuBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }
    device->postGoMenu();
}

void ToolForm::on_appSwitchBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }
    device->postAppSwitch();
}

void ToolForm::on_powerBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }
    device->postPower();
}

void ToolForm::on_screenShotBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }
    device->screenshot();
}

void ToolForm::on_volumeUpBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }
    device->postVolumeUp();
}

void ToolForm::on_volumeDownBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }
    device->postVolumeDown();
}

void ToolForm::on_closeScreenBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }
    device->setDisplayPower(false);
}

void ToolForm::on_expandNotifyBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }
    if (m_view) m_view->expandSystemPanel(false);
}

void ToolForm::on_expandSettingsBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (device) {
        if (m_view) m_view->expandSystemPanel(true);
    }
}

void ToolForm::on_touchBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }

    m_showTouch = !m_showTouch;
    device->showTouch(m_showTouch);
}

void ToolForm::on_cameraTorchBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device || !device->isCameraMode()) {
        return;
    }
    m_cameraTorch = !m_cameraTorch;
    device->setCameraTorch(m_cameraTorch);
    ui->cameraTorchBtn->setStyleSheet(m_cameraTorch ? "color: #f0c419" : "");
}

void ToolForm::on_cameraZoomOutBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (device && device->isCameraMode()) {
        device->cameraZoomOut();
    }
}

void ToolForm::on_cameraZoomInBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (device && device->isCameraMode()) {
        device->cameraZoomIn();
    }
}

void ToolForm::on_groupControlBtn_clicked()
{
    m_isHost = !m_isHost;
    updateGroupControl();
}

void ToolForm::on_openScreenBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }
    device->setDisplayPower(true);
}

void ToolForm::on_clipboardBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }
    device->requestDeviceClipboard();
}

void ToolForm::on_actionMacroBtn_clicked()
{
    openActionMacro();
}

void ToolForm::openActionMacro(bool editKeymap)
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device || device->isCameraMode()) {
        return;
    }
    if (!m_actionMacroDialog) {
        auto *video = m_view.data();
        m_actionMacroDialog = new ActionMacroDialog(m_serial, this, video ? video->appSession() : nullptr);
    }
    m_actionMacroDialog->show();
    m_actionMacroDialog->raise();
    m_actionMacroDialog->activateWindow();
    if (editKeymap) QTimer::singleShot(0, m_actionMacroDialog, [this] { if (m_actionMacroDialog) m_actionMacroDialog->openKeymapEditor(); });
}
