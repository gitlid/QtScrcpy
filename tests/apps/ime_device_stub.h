#pragma once
#include "../../QtScrcpy/QtScrcpyCore/include/QtScrcpyCore.h"
class ImeDevice final : public qsc::IDevice {
public:
    explicit ImeDevice(const QString &transport = QStringLiteral("192.0.2.1:5555")) : serial(transport) {}
    QString serial = "192.0.2.1:5555", script;
    bool playing = false, paused = false, recording = false;
    bool matchingScreen = true, applicationBound = false;
    int plays = 0, pauses = 0, resumes = 0;
    QStringList applied;
    void changed() { emit actionMacroStateChanged(recording, playing, 4); }
    void setUserData(void *) override {} void *getUserData() override { return nullptr; }
    void registerDeviceObserver(qsc::DeviceObserver *) override {} void deRegisterDeviceObserver(qsc::DeviceObserver *) override {}
    bool connectDevice() override { return true; } void disconnectDevice() override { emit deviceDisconnected(serial); }
    void mouseEvent(const QMouseEvent *, const QSize &, const QSize &) override {}
    void wheelEvent(const QWheelEvent *, const QSize &, const QSize &) override {}
    void keyEvent(const QKeyEvent *, const QSize &, const QSize &) override {}
    int backCount = 0; QStringList panelRequests;
    void postGoBack() override { ++backCount; } void postGoHome() override {} void postGoMenu() override {} void postAppSwitch() override {}
    void postPower() override {} void postVolumeUp() override {} void postVolumeDown() override {}
    void postCopy() override {} void postCut() override {} void setDisplayPower(bool) override {}
    void expandNotificationPanel() override { panelRequests.append("notifications"); } void expandSettingsPanel() override { panelRequests.append("settings"); } void collapsePanel() override {}
    void rotateDevice() override {} void startApp(const QString &) override {} void resizeDisplay(const QSize &) override {}
    void postBackOrScreenOn(bool) override {} void postTextInput(QString &) override {} void requestDeviceClipboard() override {}
    void setDeviceClipboard(bool) override {} void clipboardPaste() override {} void pushFileRequest(const QString &, const QString &) override {}
    void installApkRequest(const QString &) override {} void screenshot() override {} void showTouch(bool) override {}
    bool isReversePort(quint16) override { return false; } const QString &getSerial() override { return serial; }
    void updateScript(QString value) override { script = value; } bool isCurrentCustomKeymap() override { return !script.isEmpty(); }
    bool applyAppKeymap(const QString &value) override { script = value; applied.append(value); return true; }
    void setActionMacroApplicationBound(bool value) override { applicationBound = value; }
    bool actionMacroScreenMatches() const override { return matchingScreen; }
    bool isUhidKeyboardEnabled() const override { return true; } void releaseKeyboard() override {}
    bool startActionRecording() override { recording = true; changed(); return true; }
    bool stopActionRecording() override { recording = false; changed(); return true; }
    bool saveActionMacro(const QString &, QString *) const override { return true; }
    bool loadActionMacro(const QString &, QString *) override { return true; }
    bool playActionMacro(int, int) override { playing = true; paused = false; ++plays; changed(); return true; }
    bool playActionMacroAdvanced(int r, int i, double, qint64) override { return playActionMacro(r, i); }
    bool pauseActionMacro() override { if (!playing || paused) return false; paused = true; ++pauses; changed(); return true; }
    bool resumeActionMacro() override { if (!playing || !paused) return false; paused = false; ++resumes; changed(); return true; }
    bool isActionPaused() const override { return paused; }
    void stopActionPlayback() override { playing = paused = false; changed(); }
    bool isActionRecording() const override { return recording; } bool isActionPlaying() const override { return playing; }
    int actionMacroEventCount() const override { return 4; }
    QStringList pasted;
    QList<int> shortcuts;
    bool succeed = true;
    bool pasteImeText(const QString &text) override { if (!succeed) return false; pasted.append(text); return true; }
    bool sendImeShortcut(int which) override { if (!succeed) return false; shortcuts.append(which); return true; }
};
