#include "phonecursoradb.h"
#include "../QtScrcpyCore/src/adb/adbprocessimpl.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QUuid>

AdbPhoneCursor::AdbPhoneCursor(QObject *parent) : PhoneCursorTransport(parent) {
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] { fail(tr("手机光标启动超时。")); });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (m_stage != Idle && error == QProcess::FailedToStart) fail(tr("无法启动 ADB：") + m_process.errorString());
    });
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        const auto data = m_process.readAllStandardError();
        if (m_error.size() < 2048) m_error += data.left(2048 - m_error.size());
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        const auto bytes = m_process.readAllStandardOutput();
        if (m_stage != Run) return;
        m_output += bytes;
        if (m_output.size() > 4096) { fail(tr("手机光标响应异常。")); return; }
        while (m_output.contains('\n')) {
            const int end = m_output.indexOf('\n');
            const auto line = m_output.left(end).trimmed(); m_output.remove(0, end + 1);
            // The helper unlinks its unique uploaded file before this handshake.
            if (line == "READY 1") { m_remote.clear(); m_timeout.stop(); emit ready(); }
            else if (line == "OK") emit acknowledged();
            else if (line.startsWith("ERROR")) { fail(tr("手机系统不支持光标显示层：") + QString::fromUtf8(line)); return; }
        }
    });
    connect(&m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus status) {
            if (m_stage == Idle) return;
            if (m_stage == Push && code == 0 && status == QProcess::NormalExit) {
                m_stage = Run; m_error.clear();
                m_process.start(AdbProcessImpl::getAdbPath(), {"-s", m_serial, "shell", "-T", "CLASSPATH=" + m_remote,
                    "app_process", "/", "com.qtscrcpy.cursor.PhoneCursor", m_remote});
                m_timeout.start(8000);
            } else fail(tr("手机光标已停止：") + QString::fromUtf8(m_error.left(300)));
        });
}
AdbPhoneCursor::~AdbPhoneCursor() { close(); }
void AdbPhoneCursor::open(const QString &serial) {
    close(); m_serial = serial;
    const QString jar = QCoreApplication::applicationDirPath() + "/qtscrcpy-cursor.jar";
    if (serial.isEmpty() || !QFileInfo(jar).isFile()) { emit failure(tr("缺少手机光标组件 qtscrcpy-cursor.jar，请使用完整运行包。")); return; }
    m_remote = "/data/local/tmp/qtscrcpy-cursor-" + QUuid::createUuid().toString(QUuid::Id128) + ".jar";
    m_stage = Push; m_output.clear(); m_error.clear();
    m_timeout.start(10000);
    m_process.start(AdbProcessImpl::getAdbPath(), {"-s", m_serial, "push", jar, m_remote});
}
void AdbPhoneCursor::send(const QByteArray &line) {
    if (m_stage == Run && m_process.state() == QProcess::Running && m_process.bytesToWrite() < 256) m_process.write(line);
}
void AdbPhoneCursor::stopProcess() {
    const bool running = m_stage == Run;
    m_stage = Idle; m_timeout.stop();
    if (m_process.state() != QProcess::NotRunning) {
        if (running) { m_process.write("Q\n"); m_process.closeWriteChannel(); }
        if (!m_process.waitForFinished(running ? 300 : 10)) {
            m_process.kill(); m_process.waitForFinished(300);
        }
    }
}
void AdbPhoneCursor::close() {
    stopProcess();
    if (!m_remote.isEmpty()) {
        // Normal startup unlinks its own JAR. Also clean failed/aborted uploads.
        auto *cleanup = new QProcess(QCoreApplication::instance());
        connect(cleanup, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), cleanup, &QObject::deleteLater);
        connect(cleanup, &QProcess::errorOccurred, cleanup, &QObject::deleteLater);
        QTimer::singleShot(2000, cleanup, [cleanup] { cleanup->kill(); cleanup->deleteLater(); });
        cleanup->start(AdbProcessImpl::getAdbPath(), {"-s", m_serial, "shell", "rm", "-f", m_remote});
        m_remote.clear();
    }
}
void AdbPhoneCursor::fail(const QString &reason) { close(); emit failure(reason); }
