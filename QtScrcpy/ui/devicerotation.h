#ifndef DEVICEROTATION_H
#define DEVICEROTATION_H
#include "appsession.h"
#include <QJsonObject>
#include <QLockFile>
#include <QQueue>
#include <memory>

struct RotationState {
    int current = -1, portrait = -1, landscape = -1;
    int userRotation = -1, userMode = -1;
    bool fixedEffective = false, fixedExact = false;
    QString fixedPolicy, accelerometer, rotationSetting;
    static bool parseDisplay(const QString &text, RotationState *state);
    static bool validPolicy(const QString &value);
    static bool validSetting(const QString &value, int maximum);
    QJsonObject json() const;
    static bool fromJson(const QJsonObject &object, RotationState *state);
    bool sameSettings(const RotationState &other) const;
};

// Operates on display 0 only. Saves a durable, per-physical-device AND Android
// user recovery record before the first write; never guesses unreadable state.
class DeviceRotation : public QObject {
    Q_OBJECT
public:
    enum Mode { Toggle, Landscape, Portrait, Restore };
    DeviceRotation(const QString &serial, QObject *parent = nullptr,
                   AppCommands *commands = nullptr, const QString &directory = QString());
    ~DeviceRotation() override;
    bool busy() const { return m_busy; }
    QString backupPath() const { return m_path; }
    void request(Mode mode);
    void confirm(bool accepted);
    void disconnectDevice();
    void setWriteGuard(std::function<bool()> guard) { m_writeGuard = std::move(guard); }
signals:
    void busyChanged(bool busy);
    void confirmationRequired(const QString &message);
    void finished(bool success, const QString &message);
private:
    using Reply = std::function<void(bool, const QString &, const QString &)>;
    using StateReply = std::function<void(bool, const RotationState &, const QString &)>;
    void send(const QStringList &args, Reply reply);
    void readState(StateReply reply);
    void prepare();
    bool readBackup(QString *error);
    bool saveBackup();
    QQueue<QStringList> restorePlan(const RotationState &state) const;
    void nextWrite();
    void verify();
    void fail(const QString &reason, bool safeRollback = true);
    void finish(bool success, const QString &message);
    QStringList wm(const QString &command, const QStringList &args = QStringList()) const;
    QStringList setting(const QString &verb, const QString &key, const QString &value = QString()) const;
    QString m_serial, m_directory, m_identity, m_key, m_path;
    QString m_userCommand, m_fixedCommand, m_failure;
    int m_user = -1, m_sequence = 0, m_generation = 0, m_verifyAttempts = 0;
    Mode m_mode = Toggle;
    AppCommands *m_commands;
    QString m_pendingTag;
    Reply m_reply;
    RotationState m_before, m_original, m_expected;
    QQueue<QStringList> m_writes;
    std::unique_ptr<QLockFile> m_lock;
    std::function<bool()> m_writeGuard;
    bool m_busy = false, m_connected = true, m_confirming = false;
    bool m_hadBackup = false, m_mutated = false, m_rollback = false;
};
#endif
