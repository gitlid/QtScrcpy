#ifndef TOOLFORM_H
#define TOOLFORM_H

#include <QPointer>
#include <QWidget>

#include "../QtScrcpyCore/include/QtScrcpyCore.h"

namespace Ui
{
    class ToolForm;
}

class Device;
class ActionMacroDialog;
class DeviceRotationMenu;
class VideoForm;
class ToolForm : public QWidget
{
    Q_OBJECT

public:
    explicit ToolForm(VideoForm *view, QWidget *parent = nullptr);
    ~ToolForm();

    void setSerial(const QString& serial);
    bool isHost();
    void openActionMacro(bool editKeymap = false);

private slots:
    void on_fullScreenBtn_clicked();
    void on_returnBtn_clicked();
    void on_homeBtn_clicked();
    void on_menuBtn_clicked();
    void on_appSwitchBtn_clicked();
    void on_powerBtn_clicked();
    void on_screenShotBtn_clicked();
    void on_volumeUpBtn_clicked();
    void on_volumeDownBtn_clicked();
    void on_closeScreenBtn_clicked();
    void on_expandNotifyBtn_clicked();
    void on_expandSettingsBtn_clicked();
    void on_touchBtn_clicked();
    void on_cameraTorchBtn_clicked();
    void on_cameraZoomOutBtn_clicked();
    void on_cameraZoomInBtn_clicked();
    void on_groupControlBtn_clicked();
    void on_openScreenBtn_clicked();
    void on_clipboardBtn_clicked();
    void on_actionMacroBtn_clicked();

private:
    void initStyle();
    void updateGroupControl();
    void updateCameraMode();

private:
    Ui::ToolForm *ui;
    QPointer<VideoForm> m_view;
    QString m_serial;
    bool m_showTouch = false;
    bool m_cameraTorch = false;
    bool m_isHost = false;
    QPointer<ActionMacroDialog> m_actionMacroDialog;
    QPointer<DeviceRotationMenu> m_rotationMenu;
};

#endif // TOOLFORM_H
