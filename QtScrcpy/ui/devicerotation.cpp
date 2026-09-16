#include "devicerotation.h"
#include "appcommandprocess.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

namespace {
int rotationValue(const QString &value) {
    if (value.startsWith("ROTATION_")) {
        bool ok = false; int degrees = value.mid(9).toInt(&ok);
        return ok && degrees >= 0 && degrees <= 270 && degrees % 90 == 0 ? degrees / 90 : -1;
    }
    bool ok = false; int number = value.toInt(&ok);
    return ok && number >= 0 && number <= 3 ? number : -1;
}
QString field(const QString &text, const QString &name) {
    const auto match = QRegularExpression("\\b" + name + "=([^\\s]+)").match(text);
    return match.hasMatch() ? match.captured(1) : QString();
}
bool commandInHelp(const QString &text, const QString &name) {
    return QRegularExpression("^\\s*" + QRegularExpression::escape(name) + "(?:\\s|$)",
                              QRegularExpression::MultilineOption).match(text).hasMatch();
}
bool badOutput(const QString &text) {
    return QRegularExpression("(?:^|\\n)\\s*(?:Error:|SecurityException|java\\.lang\\.|Unknown command:|Exception occurred|Permission Denial)",
                              QRegularExpression::CaseInsensitiveOption).match(text).hasMatch();
}
bool validIdentity(const QString &text) {
    return !text.isEmpty() && text != "unknown" && text != "null" && text != "0"
        && text.size() <= 256 && !text.contains('\n') && !text.contains('\r');
}
}

bool RotationState::validPolicy(const QString &value) {
    return value == "default" || value == "enabled" || value == "disabled" || value == "enabled_if_no_auto_rotation";
}
bool RotationState::validSetting(const QString &value, int maximum) {
    if (value == "null") return true;
    bool ok = false; const int number = value.toInt(&ok);
    return ok && number >= 0 && number <= maximum && QString::number(number) == value;
}
bool RotationState::parseDisplay(const QString &text, RotationState *state) {
    if (!state || text.size() > 4 * 1024 * 1024) return false;
    // Never take a secondary display's rotation when display 0 is missing.
    const QRegularExpression heading("^\\s*Display: mDisplayId=(\\d+)\\b[^\\n]*", QRegularExpression::MultilineOption);
    auto matches = heading.globalMatch(text);
    int start = -1, end = text.size();
    while (matches.hasNext()) {
        const auto match = matches.next();
        if (start >= 0) { end = match.capturedStart(); break; }
        if (match.captured(1) == "0") start = match.capturedEnd();
    }
    if (start < 0) return false;
    QString display = text.mid(start, end - start);
    const int rotationBlock = display.indexOf(QRegularExpression("\\bDisplayRotation(?:\\s|:)"));
    if (rotationBlock >= 0) display = display.mid(rotationBlock);
    RotationState result;
    result.current = rotationValue(field(display, "mRotation"));
    result.portrait = rotationValue(field(display, "mPortraitRotation"));
    result.landscape = rotationValue(field(display, "mLandscapeRotation"));
    result.userRotation = rotationValue(field(display, "mUserRotation"));
    const QString mode = field(display, "mUserRotationMode");
    result.userMode = mode == "USER_ROTATION_FREE" || mode == "0" ? 0
                    : mode == "USER_ROTATION_LOCKED" || mode == "1" ? 1 : -1;
    const QString fixed = field(display, "mFixedToUserRotation");
    if (fixed != "true" && fixed != "false") return false;
    result.fixedEffective = fixed == "true";
    result.fixedPolicy = result.fixedEffective ? "enabled" : "disabled";
    if (result.current < 0 || result.portrait < 0 || result.landscape < 0 || result.userRotation < 0
        || result.userMode < 0 || result.portrait % 2 == result.landscape % 2) return false;
    *state = result; return true;
}
QJsonObject RotationState::json() const {
    return {{"current", current}, {"portrait", portrait}, {"landscape", landscape}, {"userRotation", userRotation},
        {"userMode", userMode}, {"fixedEffective", fixedEffective}, {"fixedExact", fixedExact},
        {"fixedPolicy", fixedPolicy}, {"accelerometer", accelerometer}, {"rotationSetting", rotationSetting}};
}
bool RotationState::fromJson(const QJsonObject &o, RotationState *state) {
    if (!state) return false;
    for (const auto &name : {"current", "portrait", "landscape", "userRotation", "userMode"}) {
        const auto v = o.value(name);
        if (!v.isDouble() || v.toDouble() != v.toInt() || v.toInt() < 0 || v.toInt() > (QString(name) == "userMode" ? 1 : 3)) return false;
    }
    if (!o["fixedEffective"].isBool() || !o["fixedExact"].isBool() || !o["fixedPolicy"].isString()
        || !o["accelerometer"].isString() || !o["rotationSetting"].isString()) return false;
    RotationState r;
    r.current=o["current"].toInt(); r.portrait=o["portrait"].toInt(); r.landscape=o["landscape"].toInt();
    r.userRotation=o["userRotation"].toInt(); r.userMode=o["userMode"].toInt();
    r.fixedEffective=o["fixedEffective"].toBool(); r.fixedExact=o["fixedExact"].toBool();
    r.fixedPolicy=o["fixedPolicy"].toString(); r.accelerometer=o["accelerometer"].toString(); r.rotationSetting=o["rotationSetting"].toString();
    if (!validPolicy(r.fixedPolicy) || !validSetting(r.accelerometer,1) || !validSetting(r.rotationSetting,3)
        || r.portrait % 2 == r.landscape % 2
        || (r.accelerometer != "null" && r.userMode != (r.accelerometer == "0" ? 1 : 0))
        || (r.rotationSetting != "null" && r.userRotation != r.rotationSetting.toInt())
        || (!r.fixedExact && r.fixedPolicy != (r.fixedEffective ? "enabled" : "disabled"))) return false;
    *state=r; return true;
}
bool RotationState::sameSettings(const RotationState &other) const {
    return userMode == other.userMode && userRotation == other.userRotation
        && accelerometer == other.accelerometer && rotationSetting == other.rotationSetting
        && (fixedExact ? other.fixedExact && fixedPolicy == other.fixedPolicy : fixedEffective == other.fixedEffective);
}

DeviceRotation::DeviceRotation(const QString &serial, QObject *parent, AppCommands *commands, const QString &directory)
    : QObject(parent), m_serial(serial), m_directory(directory.isEmpty()
      ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/rotation-backups" : directory),
      m_commands(commands ? commands : new AdbAppCommands(this)) {
    connect(m_commands, &AppCommands::finished, this, [this](const QString &tag, bool ok, const QString &out, const QString &error) {
        if (!m_busy || tag != m_pendingTag) return;
        m_pendingTag.clear();
        auto reply = std::move(m_reply); m_reply = Reply();
        if (reply) reply(ok && !badOutput(out) && !badOutput(error), out, error);
    });
}
DeviceRotation::~DeviceRotation() { m_commands->cancelAll(); }
QStringList DeviceRotation::wm(const QString &command, const QStringList &args) const {
    return QStringList{"shell", "wm", command} + args; // No -d means display 0 on both Android syntaxes.
}
QStringList DeviceRotation::setting(const QString &verb, const QString &key, const QString &value) const {
    QStringList args{"shell", "settings", "--user", QString::number(m_user), verb, "system", key};
    if (!value.isEmpty()) args << value;
    return args;
}
void DeviceRotation::send(const QStringList &args, Reply reply) {
    m_pendingTag = "rotation-" + QString::number(++m_sequence);
    m_reply = std::move(reply);
    m_commands->run(m_pendingTag, m_serial, args, 5000);
}
void DeviceRotation::request(Mode mode) {
    if (!m_connected || m_busy || mode < Toggle || mode > Restore) return;
    m_busy=true; m_confirming=false; m_mutated=false; m_rollback=false; m_mode=mode;
    m_hadBackup=false; m_writes.clear(); m_failure.clear(); ++m_generation;
    emit busyChanged(true);
    send({"shell", "getprop", "ro.serialno"}, [this](bool ok, const QString &out, const QString &) {
        const QString id=out.trimmed();
        if (!ok || !validIdentity(id)) { fail(tr("无法确认手机的物理身份，未修改方向。请检查 ADB 授权及设备序列号。")); return; }
        m_identity=id;
        send({"shell", "am", "get-current-user"}, [this](bool success, const QString &value, const QString &) {
            bool numeric=false; m_user=value.trimmed().toInt(&numeric);
            if (!success || !numeric || m_user < 0) { fail(tr("无法读取 Android 当前用户，未修改方向。")); return; }
            m_key=QString::fromLatin1(QCryptographicHash::hash((m_identity+'\n'+QString::number(m_user)).toUtf8(), QCryptographicHash::Sha256).toHex());
            m_path=QDir(m_directory).filePath(m_key+".json");
            if (!QDir().mkpath(m_directory)) { fail(tr("无法创建方向备份目录，未修改手机。")); return; }
            m_lock.reset(new QLockFile(m_path+".lock"));
            if (!m_lock->tryLock(0)) { fail(tr("此手机的方向正在被另一窗口操作，请稍后重试。")); return; }
            QString error;
            if (!readBackup(&error)) { fail(error); return; }
            if (m_mode==Restore && !m_hadBackup) { finish(false,tr("这台手机、此 Android 用户没有已保存的原设置；不会擅自恢复为默认值。")); return; }
            send(wm("help"), [this](bool valid,const QString &help,const QString &) {
                if (!valid) { fail(tr("无法读取系统旋转命令，未修改方向。")); return; }
                m_userCommand=commandInHelp(help,"user-rotation") ? "user-rotation" : commandInHelp(help,"set-user-rotation") ? "set-user-rotation" : QString();
                m_fixedCommand=commandInHelp(help,"fixed-to-user-rotation") ? "fixed-to-user-rotation" : commandInHelp(help,"set-fix-to-user-rotation") ? "set-fix-to-user-rotation" : QString();
                if (m_userCommand.isEmpty() || m_fixedCommand.isEmpty()) { fail(tr("手机系统未提供受支持的旋转/固定方向命令，未修改设置。")); return; }
                readState([this](bool read,const RotationState &state,const QString &why) {
                    if (!read) { fail(why); return; }
                    m_before=state;
                    if (!m_hadBackup) m_original=state;
                    prepare();
                });
            });
        });
    });
}
void DeviceRotation::readState(StateReply reply) {
    auto state=std::make_shared<RotationState>();
    send({"shell","dumpsys","window","displays"}, [this,state,reply](bool ok,const QString &out,const QString &) {
        if (!ok || !RotationState::parseDisplay(out,state.get())) { reply(false,*state,tr("无法完整读取主屏方向状态（display 0），未猜测或覆盖设置。")); return; }
        send(setting("get","accelerometer_rotation"), [this,state,reply](bool good,const QString &text,const QString &) {
            state->accelerometer=text.trimmed();
            if (!good || !RotationState::validSetting(state->accelerometer,1)) { reply(false,*state,tr("无法读取原自动旋转设置。")); return; }
            send(setting("get","user_rotation"), [this,state,reply](bool valid,const QString &value,const QString &) {
                state->rotationSetting=value.trimmed();
                RotationState checked;
                if (!valid || !RotationState::fromJson(state->json(),&checked)) { reply(false,*state,tr("方向设置读取失败或读取期间发生变化，请重试。")); return; }
                if (m_fixedCommand != "fixed-to-user-rotation") { reply(true,*state,QString()); return; }
                send(wm(m_fixedCommand), [state,reply](bool success,const QString &policy,const QString &) {
                    if (!success || !RotationState::validPolicy(policy.trimmed())) { reply(false,*state,QObject::tr("无法读取原固定方向策略，未覆盖它。")); return; }
                    state->fixedPolicy=policy.trimmed(); state->fixedExact=true;
                    reply(true,*state,QString());
                });
            });
        });
    });
}
bool DeviceRotation::readBackup(QString *error) {
    QFile file(m_path);
    m_hadBackup=file.exists();
    if (!m_hadBackup) return true;
    QJsonParseError parse;
    if (file.open(QIODevice::ReadOnly) && file.size()<=16384) {
        const auto doc=QJsonDocument::fromJson(file.readAll(),&parse);
        const auto root=doc.object();
        if (parse.error==QJsonParseError::NoError && doc.isObject() && root["version"].toDouble(-1)==1
            && root["deviceKey"].toString()==m_key && root["androidUser"].isDouble()
            && root["androidUser"].toDouble()==m_user && root["state"].isObject()
            && RotationState::fromJson(root["state"].toObject(),&m_original)) return true;
    }
    *error=tr("原方向备份损坏或身份不匹配，已保留文件并拒绝覆盖：%1").arg(m_path);
    return false;
}
bool DeviceRotation::saveBackup() {
    if (m_hadBackup) return true; // Repeated choices must not overwrite the FIRST settings.
    QSaveFile file(m_path);
    const auto bytes=QJsonDocument(QJsonObject{{"version",1},{"deviceKey",m_key},{"androidUser",m_user},
        {"savedAt",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},{"state",m_original.json()}}).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size() && file.commit();
}
void DeviceRotation::prepare() {
    const QString action=m_mode==Toggle ? tr("切换横竖屏") : m_mode==Landscape ? tr("固定横屏") : m_mode==Portrait ? tr("固定竖屏") : tr("恢复原设置");
    QString message=tr("%1作用于此手机的主显示屏（display 0），不是仅旋转电脑画面。\n\n原自动旋转和用户方向会备份。固定方向可能影响所有应用，退出或断连后仍可能保持；请通过“恢复原设置”撤销。\n不修改分辨率、DPI、音频或系统权限。").arg(action);
    if (!m_original.fixedExact) message+=tr("\n\n此系统只提供固定方向策略的生效值，无法区分“系统默认”和“显式关闭”。恢复时会还原原有生效行为，不能保证该策略内部标记逐字一致。");
    if (m_mode==Restore) message+=tr("\n\n将使用此前为这台手机、此 Android 用户保存的原设置，而不是默认竖屏。");
    m_confirming=true; emit confirmationRequired(message);
}
void DeviceRotation::confirm(bool accepted) {
    if (!m_busy || !m_confirming) return;
    m_confirming=false;
    if (!accepted) { finish(false,tr("已取消，未修改手机方向。")); return; }
    // Re-read after the dialog, which might have been left open for minutes.
    readState([this](bool ok,const RotationState &state,const QString &why) {
        if (!ok) { fail(why); return; }
        m_before=state;
        if (!m_hadBackup) m_original=state;
        if (!saveBackup()) { fail(tr("无法写入原设置备份，未修改手机。")); return; }
        if (m_mode==Restore) { m_expected=m_original; m_writes=restorePlan(m_original); }
        else {
            const int target=m_mode==Landscape ? state.landscape : m_mode==Portrait ? state.portrait
                : state.current%2==state.portrait%2 ? state.landscape : state.portrait;
            m_expected=state; m_expected.current=target; m_expected.userRotation=target; m_expected.rotationSetting=QString::number(target);
            m_writes.enqueue(wm(m_userCommand,{"lock",QString::number(target)}));
            if (m_mode==Toggle && state.userMode==0) {
                m_writes.enqueue(wm(m_userCommand,{"free"})); m_expected.accelerometer="1";
            } else { m_expected.userMode=1; m_expected.accelerometer="0"; }
            if (m_mode!=Toggle) {
                m_writes.enqueue(wm(m_fixedCommand,{"enabled"}));
                m_expected.fixedPolicy="enabled"; m_expected.fixedEffective=true;
            }
        }
        m_verifyAttempts=0; nextWrite();
    });
}
QQueue<QStringList> DeviceRotation::restorePlan(const RotationState &state) const {
    QQueue<QStringList> plan;
    plan.enqueue(wm(m_userCommand,{"lock",QString::number(state.userRotation)}));
    plan.enqueue(wm(m_fixedCommand,{state.fixedPolicy}));
    if (state.userMode==0) plan.enqueue(wm(m_userCommand,{"free"}));
    for (const auto &pair : {qMakePair(QString("user_rotation"),state.rotationSetting),qMakePair(QString("accelerometer_rotation"),state.accelerometer)})
        plan.enqueue(pair.second=="null" ? setting("delete",pair.first) : setting("put",pair.first,pair.second));
    return plan;
}
void DeviceRotation::nextWrite() {
    if (!m_busy || !m_connected) return;
    if (m_writes.isEmpty()) {
        const int generation=m_generation;
        QTimer::singleShot(300,this,[this,generation]{ if(m_busy && m_connected && generation==m_generation) verify(); });
        return;
    }
    send({"shell","am","get-current-user"},[this](bool ok,const QString &out,const QString &) {
        if (!ok || out.trimmed()!=QString::number(m_user)) { fail(tr("Android 用户发生变化或无法确认，已停止方向写入；原备份保留。"),false); return; }
        if (m_writeGuard && !m_writeGuard()) { fail(tr("设备断开或预制操作/录制重新启动，已停止方向写入；请停止操作后恢复。"),false); return; }
        const auto args=m_writes.dequeue();
        m_mutated=true; // A timed-out command might still have reached Android.
        send(args,[this](bool success,const QString &,const QString &error) {
            if (!success) { fail(tr("系统方向命令失败：%1").arg(error.left(300))); return; }
            nextWrite();
        });
    });
}
void DeviceRotation::verify() {
    readState([this](bool ok,const RotationState &state,const QString &why) {
        const bool orientation=(m_mode==Toggle || m_mode==Restore || m_rollback || state.current==m_expected.current);
        if (!ok || !m_expected.sameSettings(state) || !orientation) {
            if (++m_verifyAttempts<4) { const int generation=m_generation;
                QTimer::singleShot(300,this,[this,generation]{if(m_busy&&generation==m_generation)verify();}); return; }
            fail(ok ? tr("命令已返回，但系统方向未达到目标。桌面/ROM 可能限制旋转。") : why);
            return;
        }
        if (m_rollback) { finish(false,m_failure+tr("\n已还原本次操作前的设置；首次原设置备份仍保留。")); return; }
        if (m_mode==Restore) {
            if (!QFile::remove(m_path)) { finish(false,tr("手机设置已恢复，但无法移除备份，请保留并检查：%1").arg(m_path)); return; }
            finish(true,m_original.fixedExact ? tr("已核对并恢复原自动旋转、用户方向和固定方向策略。")
                : tr("已恢复原自动旋转、用户方向及固定策略的原生效行为（此系统不提供原内部策略标记）。"));
        } else if (m_mode==Toggle) {
            finish(true,state.current==m_expected.current ? tr("横竖屏切换已生效；原设置备份已保留。")
                : tr("切换请求已执行，但当前应用或自动旋转未保持目标方向。需要强制时请选择“固定横屏/固定竖屏”。"));
        } else finish(true,(m_mode==Landscape ? tr("系统已固定横屏。") : tr("系统已固定竖屏。"))
            +tr("原设置备份已保留；退出程序不会自动撤销，请使用“恢复原设置”。"));
    });
}
void DeviceRotation::fail(const QString &reason,bool safeRollback) {
    if (m_mutated && !m_rollback && safeRollback && m_connected && (!m_writeGuard || m_writeGuard())) {
        m_failure=reason; m_rollback=true; m_expected=m_before; m_writes=restorePlan(m_before); m_verifyAttempts=0;
        nextWrite(); return;
    }
    finish(false,reason+(m_mutated ? tr("\n可能已有部分设置生效，备份未删除。请在同一手机重新连接后选择“恢复原设置”。") : QString()));
}
void DeviceRotation::finish(bool success,const QString &message) {
    m_commands->cancelAll(); m_pendingTag.clear(); m_reply=Reply(); m_writes.clear();
    ++m_generation; m_confirming=false; m_busy=false; m_lock.reset();
    emit busyChanged(false); emit finished(success,message);
}
void DeviceRotation::disconnectDevice() {
    m_connected=false;
    if (m_busy) finish(false,tr("手机已断开，无法确认或恢复方向。已保存的备份保留，重新连接后可恢复。"));
}
