#include "phonecursor.h"
#include "phonecursoradb.h"
#include "../QtScrcpyCore/include/viewgeometry.h"

PhoneCursor::PhoneCursor(const QString &serial, QObject *parent, PhoneCursorTransport *transport)
    : QObject(parent), m_transport(transport ? transport : new AdbPhoneCursor(this)), m_serial(serial) {
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        // Sample and send in the same callback; two independent timers add lag.
        emit sampleRequested();
        tick();
        if (!m_enabled) return;
        m_nextFrameNs += 1000000000LL / 60;
        const auto now = m_clock.nsecsElapsed();
        if (m_nextFrameNs <= now) m_nextFrameNs = now + 1000000000LL / 60;
        m_timer.start(int((m_nextFrameNs - now + 999999) / 1000000));
    });
    connect(m_transport, &PhoneCursorTransport::ready, this, [this] {
        if (!m_enabled) return;
        m_ready = true; tick();
    });
    connect(m_transport, &PhoneCursorTransport::acknowledged, this, [this](quint64 sequence) {
        if (sequence <= m_acknowledged || sequence > m_sequence) return;
        m_acknowledged = sequence;
        // Android may coalesce several pending moves; acknowledgements are cumulative.
        while (!m_pending.isEmpty() && m_pending.head().first <= sequence) m_pending.dequeue();
    });
    connect(m_transport, &PhoneCursorTransport::failure, this, [this](const QString &message) {
        if (!m_enabled) return;
        setEnabled(false); emit failure(message);
    });
}
PhoneCursor::~PhoneCursor() { setEnabled(false); }
bool PhoneCursor::supports(const qsc::DeviceParams &p) {
    return p.display && p.videoSource == qsc::VIDEO_SOURCE_DISPLAY && p.displayId == 0 && p.newDisplay.isEmpty()
        && !p.flexDisplay && p.crop.isEmpty() && p.captureOrientationLock == 0 && p.captureOrientation == 0;
}
QByteArray PhoneCursor::position(const QPoint &local, const QSize &view, int turns, const QSize &frame) {
    if (view.width() < 2 || view.height() < 2 || frame.width() < 2 || frame.height() < 2
        || frame.width() > 32768 || frame.height() > 32768 || !QRect(QPoint(), view).contains(local)) return "H\n";
    const auto p = qsc::ViewGeometry::inverseUnit(QPointF(double(local.x()) / (view.width() - 1),
                                                       double(local.y()) / (view.height() - 1)), turns);
    return "P " + QByteArray::number(qRound(p.x() * 1000000)) + ' ' + QByteArray::number(qRound(p.y() * 1000000))
        + ' ' + QByteArray::number(frame.width()) + ' ' + QByteArray::number(frame.height()) + '\n';
}
void PhoneCursor::setEnabled(bool enabled) {
    if (enabled == m_enabled) return;
    m_enabled = enabled; m_ready = false; m_pending.clear(); m_sequence = 0; m_acknowledged = 0;
    m_desired = "H\n"; m_sent.clear(); m_lastSend.invalidate();
    if (enabled) { m_clock.start(); m_nextFrameNs = 1000000000LL / 60; m_timer.start(17); m_transport->open(m_serial); }
    else { m_timer.stop(); m_transport->close(); }
    emit enabledChanged(m_enabled);
}
void PhoneCursor::update(const QPoint &local, const QSize &view, int turns, const QSize &frame, bool visible) {
    m_desired = visible ? position(local, view, turns, frame) : QByteArray("H\n");
}
void PhoneCursor::hide() { m_desired = "H\n"; tick(); }
void PhoneCursor::tick() {
    if (!m_enabled || !m_ready) return;
    if (!m_pending.isEmpty()) {
        if (m_clock.elapsed() - m_pending.head().second > 1200) {
            setEnabled(false); emit failure(tr("手机光标连接超时，已自动关闭。请检查连接后重试。"));
            return;
        }
        if (m_pending.size() >= 3) return;
    }
    if (m_desired == m_sent && m_lastSend.isValid() && m_lastSend.elapsed() < 250) return;
    m_sent = m_desired; m_lastSend.start();
    m_pending.enqueue(qMakePair(++m_sequence, m_clock.elapsed()));
    m_transport->send(m_sent.left(1) + ' ' + QByteArray::number(m_sequence) + m_sent.mid(1));
}
