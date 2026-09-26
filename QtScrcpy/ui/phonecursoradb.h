#pragma once
#include "phonecursor.h"
#include <QProcess>

class AdbPhoneCursor final : public PhoneCursorTransport {
    Q_OBJECT
public:
    explicit AdbPhoneCursor(QObject *parent = nullptr);
    ~AdbPhoneCursor() override;
    void open(const QString &serial) override;
    void send(const QByteArray &line) override;
    void close() override;
private:
    void stopProcess();
    void fail(const QString &reason);
    QProcess m_process;
    QTimer m_timeout;
    QString m_serial, m_remote;
    QByteArray m_output, m_error;
    enum Stage { Idle, Push, Run } m_stage = Idle;
};
