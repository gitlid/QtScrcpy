#include <QApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTcpSocket>
#include <QTimer>
#include <functional>
#include <iostream>
#include <memory>
#include "controller.h"
#include "controlmsg.h"
#include "devicemsg.h"
#include "uhidkeyboard.h"

namespace {
void drain(int milliseconds = 20)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}
void key(Controller &controller, int value, bool down, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QKeyEvent event(down ? QEvent::KeyPress : QEvent::KeyRelease, value, modifiers);
    controller.keyEvent(&event, QSize(100, 200), QSize(100, 200));
    drain();
}
bool wireProtocol()
{
    ControlMsg create(ControlMsg::CMT_UHID_CREATE), input(ControlMsg::CMT_UHID_INPUT), destroy(ControlMsg::CMT_UHID_DESTROY);
    const QByteArray wire = create.serializeData();
    input.setUhidKeyboardReport(QByteArray::fromHex("0300040000000000"));
    return wire.left(8) == QByteArray::fromHex("0c00010000000011")
        && wire.mid(8, 17) == "QtScrcpy keyboard" && wire.mid(25, 2) == QByteArray::fromHex("003f")
        && wire.mid(27) == ControlMsg::uhidKeyboardDescriptor()
        && input.serializeData() == QByteArray::fromHex("0d000100080300040000000000")
        && destroy.serializeData() == QByteArray::fromHex("0e0001");
}
bool mappingAndRepeat()
{
    UhidKeyboard keyboard;
    QKeyEvent shift(QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier);
    if (keyboard.update(shift) != QByteArray::fromHex("0200000000000000")) { return false; }
    QKeyEvent a(QEvent::KeyPress, Qt::Key_A, Qt::ShiftModifier);
    if (keyboard.update(a) != QByteArray::fromHex("0200040000000000")) { return false; }
    QKeyEvent repeat(QEvent::KeyPress, Qt::Key_A, Qt::ShiftModifier, "A", true);
    if (!keyboard.update(repeat).isEmpty() || !keyboard.update(a).isEmpty()) { return false; }
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_A, Qt::ShiftModifier);
    if (keyboard.update(release) != QByteArray::fromHex("0200000000000000")) { return false; }
    keyboard.clear();
    for (int k = Qt::Key_A; k <= Qt::Key_Z; ++k) {
        QKeyEvent e(QEvent::KeyPress, k, Qt::NoModifier);
        if (UhidKeyboard::usageForEvent(e) != k - Qt::Key_A + 4) { return false; }
    }
    QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QKeyEvent keypad(QEvent::KeyPress, Qt::Key_7, Qt::KeypadModifier);
    if (UhidKeyboard::usageForEvent(tab) != 43 || UhidKeyboard::usageForEvent(keypad) != 95) { return false; }
#ifdef Q_OS_WIN
    // Q is the logical key on a non-US layout, but scan 0x1e is physical A.
    QKeyEvent position(QEvent::KeyPress, Qt::Key_Q, Qt::NoModifier, 0x1e, 0, 0);
    QKeyEvent rightCtrl(QEvent::KeyPress, Qt::Key_Control, Qt::ControlModifier, 0x11d, 0, 0);
    if (UhidKeyboard::usageForEvent(position) != 4 || UhidKeyboard::usageForEvent(rightCtrl) != 228) { return false; }
#endif
    return true;
}
bool rolloverRecovery()
{
    UhidKeyboard keyboard;
    QByteArray report;
    for (int k = Qt::Key_A; k <= Qt::Key_G; ++k) {
        QKeyEvent event(QEvent::KeyPress, k, Qt::NoModifier);
        report = keyboard.update(event);
    }
    if (report != QByteArray::fromHex("0000010101010101")) { return false; }
    QKeyEvent up(QEvent::KeyRelease, Qt::Key_G, Qt::NoModifier);
    if (keyboard.update(up) != QByteArray::fromHex("0000040506070809")) { return false; }
    keyboard.clear();
    return keyboard.update(up).isEmpty();
}
bool schemaValidation()
{
    const QJsonObject good{{"type", 13}, {"modifiers", 3}, {"keys", QJsonArray{4, 5}}};
    std::unique_ptr<ControlMsg> message(ControlMsg::fromJson(good));
    if (!message || message->toJson().value("kind") != "hid_keyboard") { return false; }
    const QVector<QJsonArray> badKeys{{4,4}, {0}, {2}, {102}, {-1}, {4.5}, {true}, {"4"}, {1}, {4,5,6,7,8,9,10}};
    for (const auto &keys : badKeys) {
        auto bad = good; bad["keys"] = keys;
        std::unique_ptr<ControlMsg> parsed(ControlMsg::fromJson(bad));
        if (parsed) { return false; }
    }
    for (const QJsonValue &modifier : QVector<QJsonValue>{-1, 256, 1.5, true, "1"}) {
        auto bad = good; bad["modifiers"] = modifier;
        std::unique_ptr<ControlMsg> parsed(ControlMsg::fromJson(bad));
        if (parsed) { return false; }
    }
    for (int type : {12, 14}) {
        auto bad = good; bad["type"] = type;
        std::unique_ptr<ControlMsg> parsed(ControlMsg::fromJson(bad));
        if (parsed) { return false; }
    }
    return true;
}
bool deviceFeedback()
{
    Controller controller([](const QByteArray &b) { return qint64(b.size()); });
    const QByteArray wire = QByteArray::fromHex("020001000103");
    for (int size = 0; size < wire.size(); ++size) {
        DeviceMsg message;
        QByteArray prefix = wire.left(size);
        if (message.deserialize(prefix) != 0) { return false; }
    }
    DeviceMsg message;
    QByteArray combined = wire + QByteArray::fromHex("010000000000000001");
    if (message.deserialize(combined) != 6) { return false; }
    controller.recvDeviceMsg(&message);
    return controller.keyboardLeds() == 3;
}
bool lifecycleAndFocus()
{
    QVector<QByteArray> sent;
    Controller controller([&](const QByteArray &b) { sent.append(b); return qint64(b.size()); });
    controller.setFrameSize(QSize(100, 200));
    if (!controller.setUhidKeyboardEnabled(true) || sent.size() != 1 || sent[0].at(0) != 12) { return false; }
    key(controller, Qt::Key_A, true);
    if (sent.last().at(0) != 13 || sent.last().at(7) != 4) { return false; }
    controller.releaseKeyboard();
    if (sent.last().mid(5) != QByteArray(8, 0)) { return false; }
    if (!controller.setUhidKeyboardEnabled(false) || sent.last() != QByteArray::fromHex("0e0001")) { return false; }
    key(controller, Qt::Key_A, true);
    if (sent.last().at(0) != 0) { return false; }
    controller.setUhidKeyboardEnabled(true);
    QKeyEvent pending(QEvent::KeyPress, Qt::Key_B, Qt::NoModifier);
    controller.keyEvent(&pending, QSize(100, 200), QSize(100, 200));
    controller.setUhidKeyboardEnabled(false);
    const int afterDestroy = sent.size();
    drain();
    if (sent.size() != afterDestroy || sent.last() != QByteArray::fromHex("0e0001")) { return false; }
    controller.setCameraMode(true);
    return !controller.setUhidKeyboardEnabled(true);
}
bool macroRoundTrip()
{
    QTemporaryDir directory;
    QVector<QByteArray> sent;
    Controller controller([&](const QByteArray &b) { sent.append(b); return qint64(b.size()); });
    controller.setFrameSize(QSize(100, 200));
    if (!controller.setUhidKeyboardEnabled(true) || !controller.startActionRecording()) { return false; }
    key(controller, Qt::Key_A, true);
    drain(50);
    // Stop an incomplete hold: the persisted macro must contain a release.
    if (!controller.stopActionRecording()) { return false; }
    const QString path = directory.filePath("keyboard.qsmacro.json");
    if (!controller.saveActionMacro(path)) { return false; }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { return false; }
    const auto json = QJsonDocument::fromJson(file.readAll()).object();
    if (json["version"] != 2 || json["events"].toArray().size() < 2) { return false; }
    if (json["events"].toArray().last().toObject()["message"].toObject()["keys"].toArray().size() != 0) { return false; }
    controller.setUhidKeyboardEnabled(false);
    sent.clear();
    if (!controller.loadActionMacro(path) || !controller.playActionMacro(2, 10)) { return false; }
    drain(300);
    if (controller.isActionPlaying() || sent.isEmpty() || sent.first().at(0) != 12 || sent.last().at(0) != 14) { return false; }
    int presses = 0;
    for (const auto &wire : sent) { if (wire.size() == 13 && wire[0] == 13 && wire[7] == 4) { ++presses; } }
    if (presses != 2) { return false; }
    // Old files keep API injection even while the manual keyboard is UHID.
    controller.setUhidKeyboardEnabled(true);
    QJsonObject legacy{{"format", "QtScrcpyActionMacro"}, {"version", 1}, {"events", QJsonArray{
        QJsonObject{{"atMs", 0}, {"message", QJsonObject{{"type", 0}, {"action", 0}, {"keycode", 29}, {"repeat", 0}, {"metastate", 0}}}}
    }}};
    QFile old(directory.filePath("old.json"));
    if (!old.open(QIODevice::WriteOnly)) { return false; }
    old.write(QJsonDocument(legacy).toJson()); old.close(); sent.clear();
    if (!controller.loadActionMacro(old.fileName()) || !controller.playActionMacro(1, 0)) { return false; }
    drain(50);
    for (const auto &wire : sent) { if (wire[0] == 0) { return true; } }
    return false;
}
bool emergencyAndTransport()
{
    QTemporaryDir directory;
    QVector<QByteArray> sent;
    bool fail = false;
    Controller controller([&](const QByteArray &b) { if (fail) { return qint64(-1); } sent.append(b); return qint64(b.size()); });
    controller.setFrameSize(QSize(100, 200));
    if (!controller.setUhidKeyboardEnabled(true) || !controller.startActionRecording()) { return false; }
    key(controller, Qt::Key_A, true);
    drain(100);
    controller.stopActionRecording();
    const QString path = directory.filePath("stop.json");
    if (!controller.saveActionMacro(path) || !controller.playActionMacro(0, 0)) { return false; }
    drain(30);
    controller.stopActionPlayback();
    if (controller.isActionPlaying() || sent.last().mid(5) != QByteArray(8,0)) { return false; }
    const int size = sent.size(); drain(200);
    if (size != sent.size() || !controller.startActionRecording()) { return false; }
    fail = true;
    key(controller, Qt::Key_B, true);
    return !controller.isActionRecording();
}
bool gameMapping()
{
    const QByteArray script(R"({"switchKey":"Key_QuoteLeft","keyMapNodes":[
        {"type":"KMT_CLICK","key":"Key_W","pos":{"x":0.5,"y":0.5},"switchMap":false,"androidKey":0}
    ]})");
    QVector<QByteArray> sent;
    Controller controller([&](const QByteArray &b) { sent.append(b); return qint64(b.size()); }, QString::fromUtf8(script));
    if (!controller.setUhidKeyboardEnabled(true)) { return false; }
    key(controller, Qt::Key_QuoteLeft, true);
    if (!controller.isCurrentCustomKeymap()) { return false; }
    sent.clear();
    key(controller, Qt::Key_W, true);
    key(controller, Qt::Key_W, false);
    bool touch = false;
    for (const auto &wire : sent) {
        if (wire[0] == 13) { return false; }
        if (wire[0] == 2) { touch = true; }
    }
    return touch;
}
int live(quint16 port, const QString &directory)
{
    QTcpSocket socket;
    socket.connectToHost("127.0.0.1", port);
    if (!socket.waitForConnected(5000)) { return 1; }
    Controller controller([&](const QByteArray &data) {
        const qint64 n = socket.write(data);
        socket.flush();
        return n;
    });
    controller.setFrameSize(QSize(100,200));
    if (!controller.setUhidKeyboardEnabled(true)) { return 2; }
    drain(500);
    std::cout << "READY" << std::endl;
    std::string command;
    std::getline(std::cin, command);
    if (command != "run") { return 3; }
    // Focus loss during an ordinary modifier hold.
    key(controller, Qt::Key_Shift, true, Qt::ShiftModifier);
    drain(80);
    controller.releaseKeyboard();
    drain(80);
    if (!controller.startActionRecording()) { return 4; }
    key(controller, Qt::Key_A, true);
    drain(100);
    if (!controller.stopActionRecording() || !controller.saveActionMacro(directory + "/live-keyboard.qsmacro.json")) { return 5; }
    if (!controller.playActionMacro(2,100)) { return 6; }
    drain(650);
    if (controller.isActionPlaying()) { return 7; }
    std::cout << "FINISHED" << std::endl;
    std::getline(std::cin, command);
    controller.shutdownKeyboard();
    drain(100);
    socket.disconnectFromHost();
    return 0;
}
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (argc == 4 && QString::fromLocal8Bit(argv[1]) == "--live") {
        return live(QString::fromLocal8Bit(argv[2]).toUShort(), QString::fromLocal8Bit(argv[3]));
    }
    const QVector<QPair<const char *, std::function<bool()>>> cases{
        {"wire",wireProtocol}, {"mapping",mappingAndRepeat}, {"rollover",rolloverRecovery},
        {"schema",schemaValidation}, {"feedback",deviceFeedback}, {"lifecycle",lifecycleAndFocus},
        {"macro",macroRoundTrip}, {"emergency",emergencyAndTransport}, {"game_mapping",gameMapping}
    };
    int passed = 0, executed = 0;
    for (const auto &test : cases) {
        if (argc > 1 && QString::fromLocal8Bit(argv[1]) != test.first) { continue; }
        ++executed;
        const bool ok = test.second();
        std::cout << (ok ? "PASS " : "FAIL ") << test.first << std::endl;
        if (ok) { ++passed; }
    }
    return executed && passed == executed ? 0 : 1;
}
