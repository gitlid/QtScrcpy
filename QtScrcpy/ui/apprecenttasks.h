#ifndef APPRECENTTASKS_H
#define APPRECENTTASKS_H
#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include <QStringList>
#include <functional>
class AppCommands;

// An observed, current-Android-user recent-task list, not process enumeration.
class AppRecentTasks : public QObject {
    Q_OBJECT
public:
    AppRecentTasks(const QString &serial, AppCommands *commands, QObject *parent = nullptr);
    void start();
    void stop();
    void refresh();
    bool hasSnapshot() const { return m_hasSnapshot; }
    QStringList packages() const { return m_packages; }
    QString status() const { return m_status; }
    bool closing() const { return !m_closingPackage.isEmpty(); }
    bool canClose(const QString &packageName) const;
    bool closePackage(const QString &packageName);
    void setWriteGuard(std::function<bool()> guard) { m_writeGuard = std::move(guard); }
    static bool parse(const QString &output, QStringList *packages, int *user);
signals:
    void changed();
    void statusChanged();
    void userChanged();
    void closeFinished(bool success, const QString &message);
private:
    void result(const QString &tag, bool success, const QString &output, const QString &error);
    void setStatus(const QString &message);
    void finishClose(bool success, const QString &message);
    QString nextTag(const QString &kind);
    AppCommands *m_commands;
    QString m_serial, m_pollTag, m_closeTag, m_closingPackage, m_status;
    QStringList m_packages;
    QTimer m_timer;
    QElapsedTimer m_lastSuccess;
    std::function<bool()> m_writeGuard;
    quint64 m_generation = 0;
    int m_user = -1;
    bool m_running = false, m_hasSnapshot = false, m_fresh = false, m_verifyingClose = false;
};
#endif
