#include <functional>
#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include "controller.h"
#include "controlmsg.h"
#include "../QtScrcpy/ui/actionmacrodialog.h"
#include "../QtScrcpy/ui/actionmacrohotkey.h"

namespace {
void drain(int milliseconds = 20)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}
ControlMsg *down(AndroidKeycode code = AKEYCODE_A)
{
    auto *message = new ControlMsg(ControlMsg::CMT_INJECT_KEYCODE);
    message->setInjectKeycodeMsgData(AKEY_EVENT_ACTION_DOWN, code, 0, AMETA_NONE);
    return message;
}
bool videoRequired()
{
    Controller controller([](const QByteArray &data) { return qint64(data.size()); });
    if (controller.startActionRecording() || controller.playActionMacro(1, 0)) { return false; }
    controller.setFrameSize(QSize(100, 200));
    if (!controller.startActionRecording()) { return false; }
    return controller.stopActionRecording();
}
bool stopClearsQueue()
{
    QVector<QByteArray> sent;
    Controller controller([&](const QByteArray &data) { sent.append(data); return qint64(data.size()); });
    controller.setFrameSize(QSize(100, 200));
    if (!controller.startActionRecording()) { return false; }
    controller.postControlMsg(down());
    drain();
    if (sent.size() != 1 || controller.actionMacroEventCount() != 1) { return false; }
    controller.postControlMsg(down(AKEYCODE_B)); // Still queued when Stop is pressed.
    if (!controller.stopActionRecording()) { return false; }
    drain();
    return sent.size() == 2 && sent.last().size() > 1 && sent.last().at(1) == char(AKEY_EVENT_ACTION_UP)
        && controller.actionMacroEventCount() == 2;
}
bool transportFailureStops()
{
    bool fail = false;
    int errorCount = 0;
    Controller controller([&](const QByteArray &data) { return fail ? qint64(-1) : qint64(data.size()); });
    QObject::connect(&controller, &Controller::actionMacroError, &controller, [&](const QString &) { ++errorCount; });
    controller.setFrameSize(QSize(100, 200));
    if (!controller.startActionRecording()) { return false; }
    controller.postControlMsg(down());
    drain();
    fail = true;
    controller.postControlMsg(down(AKEYCODE_B));
    drain();
    return errorCount > 0 && !controller.isActionRecording() && !controller.isActionPlaying();
}
bool playbackGuards()
{
    QTemporaryDir directory;
    ControlMsg press(ControlMsg::CMT_INJECT_TOUCH);
    press.setInjectTouchMsgData(POINTER_ID_MOUSE, AMOTION_EVENT_ACTION_DOWN,
        static_cast<AndroidMotioneventButtons>(0), static_cast<AndroidMotioneventButtons>(0), QRect(10, 20, 100, 200), 1.0f);
    QJsonObject event{{"atMs", 0}, {"message", press.toJson()}};
    QJsonObject root{{"format", "QtScrcpyActionMacro"}, {"version", 1},
                     {"durationMs", 1000}, {"events", QJsonArray{event}}};
    QFile file(directory.filePath("macro.json"));
    if (!file.open(QIODevice::WriteOnly)) { return false; }
    file.write(QJsonDocument(root).toJson());
    file.close();
    QVector<QByteArray> sent;
    Controller controller([&](const QByteArray &data) { sent.append(data); return qint64(data.size()); });
    controller.setFrameSize(QSize(100, 200));
    if (!controller.loadActionMacro(file.fileName()) || !controller.playActionMacro(1, 0)) { return false; }
    controller.postControlMsg(down()); // Manual input must not mix with playback.
    drain();
    if (sent.size() != 1 || !controller.isActionPlaying()) { return false; }
    controller.setFrameSize(QSize(200, 100));
    drain();
    return !controller.isActionPlaying() && sent.size() == 2
        && sent.last().at(1) == char(AMOTION_EVENT_ACTION_UP);
}
bool disconnectedDialog()
{
    ActionMacroDialog dialog("nonexistent-action-macro-test-device");
    dialog.show();
    drain();
    const auto buttons = dialog.findChildren<QPushButton *>();
    if (buttons.size() < 5) { return false; }
    for (const auto *button : buttons) { if (button->isEnabled()) { return false; } }
    // Direct Qt delivery checks the application-level emergency-key fallback.
    QKeyEvent stop(QEvent::KeyPress, Qt::Key_X, Qt::ControlModifier | Qt::ShiftModifier);
    QApplication::sendEvent(&dialog, &stop);
    if (!stop.isAccepted()) { return false; }
    dialog.reject();
    drain();
    return !dialog.isVisible();
}
}
int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    const QVector<QPair<const char *, std::function<bool()>>> cases{
        {"video_required", videoRequired}, {"stop_clears_queue", stopClearsQueue},
        {"transport_failure", transportFailureStops}, {"playback_guards", playbackGuards},
        {"disconnected_dialog", disconnectedDialog}
    };
    int passed = 0;
    int executed = 0;
    for (const auto &test : cases) {
        if (argc > 1 && QString::fromLocal8Bit(argv[1]) != test.first) { continue; }
        ++executed;
        const bool success = test.second();
        qInfo("%s %s", success ? "PASS" : "FAIL", test.first);
        if (success) { ++passed; }
    }
    qInfo("Controller/UI integration results: %d/%d passed", passed, executed);
    return executed > 0 && passed == executed ? 0 : 1;
}
