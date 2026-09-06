#ifndef ACTIONMACROHOTKEY_H
#define ACTIONMACROHOTKEY_H

#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QKeyEvent>
#include <QPointer>
#include <QVector>
#include "../QtScrcpyCore/include/QtScrcpyCore.h"

#ifdef Q_OS_WIN
#include <windows.h>
#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#endif
#endif

// One application-wide handler prevents ambiguous per-window shortcuts.
// Windows additionally attempts an OS-global hotkey; failure is visible in UI.
class ActionMacroHotkey : public QObject, public QAbstractNativeEventFilter
{
public:
    static ActionMacroHotkey *instance()
    {
        static QPointer<ActionMacroHotkey> current;
        if (!current) { current = new ActionMacroHotkey(qApp); }
        return current.data();
    }
    void watch(qsc::IDevice *device)
    {
        if (!device) { return; }
        for (const auto &item : m_devices) { if (item == device) { return; } }
        m_devices.append(QPointer<qsc::IDevice>(device));
    }
    bool globalAvailable() const { return m_globalAvailable; }
    void stopAll()
    {
        // Work on a copy: stopping can emit signals and destroy windows/devices.
        const auto devices = m_devices;
        for (const auto &device : devices) {
            if (device) { device->stopActionPlayback(); }
            if (device) { device->stopActionRecording(); }
        }
        for (int i = m_devices.size() - 1; i >= 0; --i) {
            if (!m_devices.at(i)) { m_devices.remove(i); }
        }
    }
    ~ActionMacroHotkey() override
    {
#ifdef Q_OS_WIN
        if (m_globalAvailable) { UnregisterHotKey(nullptr, kHotkeyId); }
#endif
        if (qApp) { qApp->removeNativeEventFilter(this); qApp->removeEventFilter(this); }
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        Q_UNUSED(watched);
        if (event->type() != QEvent::ShortcutOverride && event->type() != QEvent::KeyPress
            && event->type() != QEvent::KeyRelease) { return false; }
        const auto *key = static_cast<QKeyEvent *>(event);
        const auto modifiers = key->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
        if (key->key() != Qt::Key_X || modifiers != (Qt::ControlModifier | Qt::ShiftModifier)) { return false; }
        if (event->type() == QEvent::KeyPress && !key->isAutoRepeat()) { stopAll(); }
        event->accept();
        return true;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override
#else
    bool nativeEventFilter(const QByteArray &eventType, void *message, long *result) override
#endif
    {
        Q_UNUSED(eventType);
        Q_UNUSED(result);
#ifdef Q_OS_WIN
        const MSG *msg = static_cast<const MSG *>(message);
        if (msg && msg->message == WM_HOTKEY && msg->wParam == kHotkeyId) {
            stopAll();
            return true;
        }
#else
        Q_UNUSED(message);
#endif
        return false;
    }

private:
    explicit ActionMacroHotkey(QObject *parent) : QObject(parent)
    {
        qApp->installEventFilter(this);
        qApp->installNativeEventFilter(this);
#ifdef Q_OS_WIN
        m_globalAvailable = RegisterHotKey(nullptr, kHotkeyId, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'X') != 0;
#endif
    }
    enum { kHotkeyId = 0x514D };
    bool m_globalAvailable = false;
    QVector<QPointer<qsc::IDevice>> m_devices;
};
#endif
