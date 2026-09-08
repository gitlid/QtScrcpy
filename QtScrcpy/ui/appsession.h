#ifndef APPSESSION_H
#define APPSESSION_H
#include <QElapsedTimer>
#include <QHash>
#include <QPointer>
#include <QTimer>
#include <functional>
#include "appbinding.h"
#include "../QtScrcpyCore/include/QtScrcpyCore.h"

class AppCommands : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void run(const QString &tag, const QString &serial, const QStringList &arguments, int timeoutMs) = 0;
    virtual void cancelAll() = 0;
    virtual void cancel(const QString &tag) = 0;
signals:
    void finished(const QString &tag, bool success, const QString &output, const QString &error);
};

struct PhoneApp {
    QString packageName, label, component;
};

// One instance belongs to one live video session. It never follows a replacement device.
class AppSession : public QObject {
    Q_OBJECT
public:
    AppSession(qsc::IDevice *device, const QString &serverPath, QObject *parent = nullptr,
               AppCommands *commands = nullptr, const QString &profileDirectory = QString());
    ~AppSession() override;
    void start();
    void shutdown();
    QList<PhoneApp> apps() const;
    QStringList tabs() const { return m_tabs; }
    QString foreground() const { return m_foreground; }
    QString label(const QString &packageName) const;
    AppBinding binding(const QString &packageName) const;
    QString status() const { return m_status; }
    bool locked() const { return !m_target.isEmpty(); }
    bool preparing() const { return bool(m_pendingStart); }
    bool ready() const { return m_connected && m_profilesReady; }
    void refreshApps();
    void activate(const QString &packageName);
    void closeTab(const QString &packageName);
    bool bindKeymap(const QString &packageName, const QString &script);
    QString keymap(const QString &packageName) const;
    bool rememberMacro(const AppBinding &binding, const QString &path);
    QStringList macros(const QString &packageName) const;
    void startMacro(const AppBinding &binding, std::function<bool()> play);
    void resumeMacro();
    void pauseMacro();
    void stopMacro();
    // Public parsers also serve the device-independent regression suite.
    static QList<PhoneApp> parseComponents(const QString &output);
    static QString parseForeground(const QString &output);
signals:
    void appsChanged();
    void foregroundChanged(const QString &packageName);
    void statusChanged(const QString &message);
    void lockChanged();
    void failure(const QString &message);
private:
    void tick();
    void result(const QString &tag, bool success, const QString &output, const QString &error);
    void launch(const QString &packageName);
    void observe(const QString &packageName);
    void advanceGuard();
    void deviceStateChanged();
    void applyForegroundKeymap();
    void setStatus(const QString &text);
    void failGuard(const QString &text);
    void loadProfiles(const QString &identity);
    bool saveProfiles();
    void ensureTab(const QString &packageName);
    QPointer<qsc::IDevice> m_device;
    AppCommands *m_commands;
    QTimer m_timer;
    QElapsedTimer m_lastProbe, m_recovery, m_stable;
    QHash<QString, PhoneApp> m_apps;
    QHash<QString, QJsonObject> m_profiles;
    QStringList m_tabs;
    QString m_serial, m_serverPath, m_directory, m_profilePath, m_foreground, m_target, m_status;
    QString m_appliedPackage, m_appliedScript;
    std::function<bool()> m_pendingStart;
    bool m_connected = true, m_profilesReady = false, m_readOnlyProfiles = false;
    bool m_probePending = false, m_launchPending = false, m_autoPaused = false;
    bool m_manualPaused = false, m_internal = false, m_started = false, m_hasAppKeymap = false;
    int m_launchAttempts = 0;
};
#endif
