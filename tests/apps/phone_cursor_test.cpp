#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <cstdio>
#include <stdexcept>
#include <functional>
#include "../../QtScrcpy/ui/phonecursor.h"
#include "../../QtScrcpy/ui/phonecursoradb.h"

namespace {
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void wait(int ms) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); }
void until(const std::function<bool()> &done) { QElapsedTimer elapsed; elapsed.start(); while (!done() && elapsed.elapsed() < 1500) wait(10); }
class Transport : public PhoneCursorTransport {
public:
    QList<QByteArray> lines;
    int opens = 0, closes = 0;
    bool autoReady = true, autoAck = true;
    void open(const QString &serial) override { require(serial == "test-device", "wrong device"); ++opens; if (autoReady) emit ready(); }
    void close() override { ++closes; }
    void send(const QByteArray &line) override { lines.append(line); if (autoAck) emit acknowledged(); }
};
void geometry() {
    const QSize view(101, 201), frame(720, 1560);
    require(PhoneCursor::position(QPoint(50, 100), view, 0, frame) == "P 500000 500000 720 1560\n", "normalized center");
    const QList<QByteArray> corners = {"0 0", "0 1000000", "1000000 1000000", "1000000 0"};
    for (int turn = 0; turn < 4; ++turn) {
        require(PhoneCursor::position(QPoint(), view, turn, frame) == "P " + corners[turn] + " 720 1560\n", "rotation inverse");
        require(PhoneCursor::position(QPoint(100, 200), view, turn, frame) == "P " + corners[(turn + 2) % 4] + " 720 1560\n", "edge hotspot");
    }
    require(PhoneCursor::position(QPoint(50, 100), view, -1, frame) == "P 500000 500000 720 1560\n", "negative rotation");
}
void bounds() {
    for (const QPoint &p : {QPoint(-1, 0), QPoint(100, 0), QPoint(0, 200)})
        require(PhoneCursor::position(p, QSize(100, 200), 0, QSize(720, 1560)) == "H\n", "out of video must hide");
    require(PhoneCursor::position(QPoint(), QSize(1, 200), 0, QSize(720, 1560)) == "H\n", "degenerate view");
    require(PhoneCursor::position(QPoint(), QSize(100, 200), 0, QSize()) == "H\n", "unknown source");
}
void supports() {
    const qsc::DeviceParams full;
    require(PhoneCursor::supports(full), "full main screen");
    auto p = full; p.crop = "100:200:0:0"; require(!PhoneCursor::supports(p), "crop guard");
    p = full; p.displayId = 1; require(!PhoneCursor::supports(p), "other display guard");
    p = full; p.newDisplay = "1280x720"; require(!PhoneCursor::supports(p), "virtual guard");
    p = full; p.flexDisplay = true; require(!PhoneCursor::supports(p), "flex guard");
    p = full; p.captureOrientationLock = 1; require(!PhoneCursor::supports(p), "capture lock guard");
    p = full; p.captureOrientation = 180; require(!PhoneCursor::supports(p), "capture rotation guard");
    p = full; p.videoSource = qsc::VIDEO_SOURCE_CAMERA; require(!PhoneCursor::supports(p), "camera guard");
    p = full; p.display = false; require(!PhoneCursor::supports(p), "record only guard");
}
void coalescing() {
    Transport t; t.autoAck = false; PhoneCursor c("test-device", nullptr, &t); c.setEnabled(true);
    for (int n = 0; n <= 100; ++n) c.update(QPoint(n, 50), QSize(101, 101), 0, QSize(1080, 1080), true);
    wait(80); require(t.lines.size() == 1, "must not queue moves behind unacknowledged packet");
    emit t.acknowledged(); until([&] { return t.lines.size() == 2; });
    require(t.lines.size() == 2 && t.lines.last() == "P 1000000 500000 1080 1080\n", "only newest point sent");
}
void visibility() {
    Transport t; PhoneCursor c("test-device", nullptr, &t); c.setEnabled(true);
    c.update(QPoint(50, 50), QSize(101, 101), 1, QSize(1080, 1080), true); until([&] { return t.lines.last().startsWith("P "); });
    require(t.lines.last().startsWith("P "), "visible point");
    c.update(QPoint(50, 50), QSize(101, 101), 1, QSize(1080, 1080), false); until([&] { return t.lines.last() == "H\n"; });
    require(t.lines.last() == "H\n", "hidden point");
    c.update(QPoint(50, 50), QSize(101, 101), 1, QSize(1080, 1080), true); until([&] { return t.lines.last().startsWith("P "); }); c.hide();
    require(t.lines.last() == "H\n", "hide immediate after acknowledgement");
}
void heartbeat() {
    Transport t; PhoneCursor c("test-device", nullptr, &t); c.setEnabled(true); wait(310);
    require(t.lines.size() == 2 && t.lines.last() == "H\n", "bounded heartbeat when stationary");
    c.setEnabled(false); const int before = t.lines.size(); wait(310);
    require(t.lines.size() == before && t.closes == 1, "off stops heartbeat and transport");
}
void timeout() {
    Transport t; t.autoAck = false; PhoneCursor c("test-device", nullptr, &t); int errors = 0;
    QObject::connect(&c, &PhoneCursor::failure, &c, [&](const QString &) { ++errors; });
    c.setEnabled(true); wait(1400);
    require(!c.enabled() && t.closes == 1 && errors == 1 && t.opens == 1, "timeout stops without auto restart");
}
void pendingStop() {
    Transport t; t.autoReady = false; PhoneCursor c("test-device", nullptr, &t);
    c.setEnabled(true); c.setEnabled(false); emit t.ready(); wait(50);
    require(t.lines.isEmpty() && t.closes == 1, "late startup after disable ignored");
}
void failure() {
    Transport t; PhoneCursor c("test-device", nullptr, &t); int errors = 0;
    QObject::connect(&c, &PhoneCursor::failure, &c, [&](const QString &) { ++errors; });
    c.setEnabled(true); emit t.failure("unsupported"); emit t.failure("late");
    require(!c.enabled() && t.closes == 1 && errors == 1, "failure clears toggle once");
    c.setEnabled(true); require(c.enabled() && t.opens == 2 && t.lines.last() == "H\n", "retry starts hidden");
}
void lifetime() {
    Transport t; { PhoneCursor c("test-device", nullptr, &t); c.setEnabled(true); }
    require(t.closes == 1, "destruction closes owned session");
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QString name = argc > 1 ? argv[1] : "";
    // Opt-in real-device transport smoke test. It draws only; no input events.
    if (name == "device" && argc == 5) {
        AdbPhoneCursor transport;
        PhoneCursor cursor(QString::fromUtf8(argv[2]), nullptr, &transport);
        const QSize frame(QString::fromUtf8(argv[3]).toInt(), QString::fromUtf8(argv[4]).toInt());
        QObject::connect(&cursor, &PhoneCursor::failure, &app, [&](const QString &message) {
            std::fprintf(stderr, "%s\n", message.toUtf8().constData()); app.exit(1);
        });
        QObject::connect(&transport, &PhoneCursorTransport::ready, &app, [&] {
            cursor.update(QPoint(50, 50), QSize(101, 101), 0, frame, true);
            std::puts("CENTER"); std::fflush(stdout);
            QTimer::singleShot(8000, &app, [&] {
                cursor.update(QPoint(15, 20), QSize(101, 101), 0, frame, true);
                std::puts("CORNER"); std::fflush(stdout);
            });
            QTimer::singleShot(16000, &app, [&] { cursor.hide(); std::puts("HIDDEN"); std::fflush(stdout); });
            QTimer::singleShot(20000, &app, [&] { cursor.setEnabled(false); app.quit(); });
        });
        QTimer::singleShot(30000, &app, [&] { app.exit(2); });
        cursor.setEnabled(true);
        return app.exec();
    }
    try {
        if (name == "geometry") geometry(); else if (name == "bounds") bounds(); else if (name == "supports") supports();
        else if (name == "coalescing") coalescing(); else if (name == "visibility") visibility();
        else if (name == "heartbeat") heartbeat(); else if (name == "timeout") timeout();
        else if (name == "pending_stop") pendingStop(); else if (name == "failure") failure();
        else if (name == "lifetime") lifetime(); else throw std::runtime_error("unknown test");
        std::puts("PASS"); return 0;
    } catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
