#ifndef APPCOMMANDPROCESS_H
#define APPCOMMANDPROCESS_H
#include "appsession.h"
#include "../QtScrcpyCore/src/adb/adbprocessimpl.h"
#include <QProcess>
#include <memory>

// Keep bytes until a command finishes: trimming/decode-per-chunk loses line
// separators and can split a UTF-8 Chinese application name between reads.
class AppCommandOutput {
public:
    bool append(const QByteArray &bytes, bool error = false) {
        if (bytes.size() > 4 * 1024 * 1024 - out.size() - err.size()) return false;
        (error ? err : out).append(bytes);
        return true;
    }
    QString standard() const { return QString::fromUtf8(out); }
    QString errors() const { return QString::fromUtf8(err); }
private:
    QByteArray out, err;
};

// Reuse the core's configured ADB executable, but preserve raw query output.
// Only this session's short-lived requests are owned/cancelled here.
class AdbAppCommands final : public AppCommands {
public:
    using AppCommands::AppCommands;
    ~AdbAppCommands() override { cancelAll(); }
    void run(const QString &tag, const QString &serial, const QStringList &args, int timeoutMs) override {
        if (m_jobs.contains(tag)) return;
        auto *process = new QProcess(this);
        m_jobs.insert(tag, process);
        auto output = std::make_shared<AppCommandOutput>();
        auto done = std::make_shared<bool>(false);
        auto *timer = new QTimer(process);
        timer->setSingleShot(true);
        auto finish = [this, process, timer, tag, output, done](bool ok, const QString &error) {
            if (*done) return;
            *done = true;
            timer->stop();
            const bool bounded = output->append(process->readAllStandardOutput())
                              && output->append(process->readAllStandardError(), true);
            const QString text = output->standard() + (tag == "names" ? '\n' + output->errors() : QString());
            const QString reason = !bounded ? tr("ADB 查询输出超过限制")
                                 : !error.isEmpty() ? error : output->errors().left(300);
            m_jobs.remove(tag);
            process->deleteLater();
            emit finished(tag, ok && bounded, text, reason);
        };
        connect(process, &QProcess::readyReadStandardOutput, process, [process, output, finish] {
            if (!output->append(process->readAllStandardOutput())) {
                process->kill(); finish(false, tr("ADB 查询输出超过限制"));
            }
        });
        connect(process, &QProcess::readyReadStandardError, process, [process, output, finish] {
            if (!output->append(process->readAllStandardError(), true)) {
                process->kill(); finish(false, tr("ADB 查询输出超过限制"));
            }
        });
        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), process,
                [finish](int code, QProcess::ExitStatus status) { finish(code == 0 && status == QProcess::NormalExit, QString()); });
        connect(process, &QProcess::errorOccurred, process, [process, finish](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) finish(false, process->errorString());
        });
        connect(timer, &QTimer::timeout, process, [process, finish] {
            process->kill(); finish(false, tr("ADB 请求超时"));
        });
        QStringList arguments;
        if (!serial.isEmpty()) arguments << "-s" << serial;
        arguments << args;
        timer->start(timeoutMs);
        process->start(AdbProcessImpl::getAdbPath(), arguments);
    }
    void cancelAll() override {
        const auto tags = m_jobs.keys();
        for (const auto &tag : tags) cancel(tag);
    }
    void cancel(const QString &tag) override {
        auto *process = m_jobs.take(tag);
        if (!process) return;
        process->disconnect(); process->kill(); delete process;
    }
private:
    QHash<QString, QProcess *> m_jobs;
};
#endif
