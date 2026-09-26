#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <QPointF>
#include <QSize>
#include <QTimer>
#include <QQueue>
#include "../QtScrcpyCore/include/QtScrcpyCoreDef.h"

class PhoneCursorTransport : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void open(const QString &serial) = 0;
    virtual void send(const QByteArray &line) = 0;
    virtual void close() = 0;
signals:
    void ready();
    void acknowledged(quint64 sequence);
    void failure(const QString &message);
};

class PhoneCursor : public QObject {
    Q_OBJECT
public:
    explicit PhoneCursor(const QString &serial, QObject *parent = nullptr, PhoneCursorTransport *transport = nullptr);
    ~PhoneCursor() override;
    static bool supports(const qsc::DeviceParams &params);
    static QByteArray position(const QPoint &local, const QSize &view, int turns, const QSize &frame);
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    void update(const QPoint &local, const QSize &view, int turns, const QSize &frame, bool visible);
    void hide();
signals:
    void sampleRequested();
    void enabledChanged(bool enabled);
    void failure(const QString &message);
private:
    void tick();
    PhoneCursorTransport *m_transport;
    QString m_serial;
    QTimer m_timer;
    QElapsedTimer m_lastSend;
    QElapsedTimer m_clock;
    qint64 m_nextFrameNs = 0;
    quint64 m_sequence = 0, m_acknowledged = 0;
    QQueue<QPair<quint64, qint64>> m_pending;
    QByteArray m_desired = "H\n", m_sent;
    bool m_enabled = false, m_ready = false;
};
