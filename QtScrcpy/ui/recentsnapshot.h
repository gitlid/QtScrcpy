#ifndef RECENTSNAPSHOT_H
#define RECENTSNAPSHOT_H
#include "appbinding.h"
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

// Parse only task-level identity, never affinity, child activities or Intent extras.
// All outputs are committed together, after envelope and Android-user validation.
namespace RecentSnapshot {
inline bool commandError(const QString &text) {
    return QRegularExpression(QStringLiteral(
        "(?:^|\\n)[ \\t]*(?:Error:|error:|Permission Denial|Permission denied|"
        "Exception occurred|(?:[a-zA-Z0-9_]+\\.)*[a-zA-Z0-9_]*Exception[: ])"),
        QRegularExpression::CaseInsensitiveOption).match(text).hasMatch();
}
inline QString rootFields(const QString &block) {
    const auto lines = block.split('\n');
    int indent = 100000;
    for (int i = 1; i < lines.size(); ++i) {
        if (lines[i].trimmed().isEmpty()) continue;
        int n = 0; while (n < lines[i].size() && lines[i][n].isSpace()) ++n;
        if (n > 0) indent = qMin(indent, n);
    }
    QStringList fields;
    for (int i = 1; i < lines.size(); ++i) {
        int n = 0; while (n < lines[i].size() && lines[i][n].isSpace()) ++n;
        if (n == indent) fields.append(lines[i].trimmed());
    }
    return fields.join('\n');
}
inline bool records(const QString &body, bool info, int activeUser, QStringList *out, QString *why) {
    auto fail = [why](const QString &s) { *why = s; return false; };
    const QRegularExpression header(info ? QStringLiteral("(?:^|\\n)[ \\t]*\\*?[ \\t]*RecentTaskInfo[ \\t]+#([0-9]+)[ \\t]*:")
        : QStringLiteral("(?:^|\\n)[ \\t]*\\*?[ \\t]*Recent[ \\t]+#([0-9]+)[ \\t]*:[ \\t]*(?:TaskRecord|Task)\\{"));
    QList<QRegularExpressionMatch> heads;
    auto matches = header.globalMatch(body);
    while (matches.hasNext()) { heads.append(matches.next()); if (heads.size() > 512) return fail("too-many-tasks"); }
    const QRegularExpression anyHeader(info ? "RecentTaskInfo[ \\t]+#" : "Recent[ \\t]+#");
    int claimed = 0; auto all = anyHeader.globalMatch(body); while (all.hasNext()) { all.next(); ++claimed; }
    if (heads.size() != claimed) return fail("unsupported-task-header");
    if (heads.isEmpty()) {
        if (body.contains("Task{") || body.contains("TaskRecord{") || body.contains("RecentTaskInfo")) return fail("unrecognized-task-list");
        // A header alone is a valid empty primary dump. Visible-info fallback must
        // contain records or an explicit empty marker, not a truncated heading.
        if (info && !body.contains("(nothing)")) return fail("empty-visible-section");
        out->clear(); return true;
    }
    QStringList found; QSet<QString> seen;
    for (int i = 0; i < heads.size(); ++i) {
        const int end = i + 1 < heads.size() ? heads[i + 1].capturedStart() : body.size();
        const QString block = body.mid(heads[i].capturedStart(), end - heads[i].capturedStart()).trimmed();
        const QString heading = block.section('\n', 0, 0);
        const QString fields = rootFields(block);
        // U= and uNN are accepted only on the Task header. mUserId is used by
        // some Task dumps; RecentTaskInfo uses userId. Conflicting IDs fail closed.
        QSet<int> users;
        const QString identity = heading + '\n' + fields;
        const QRegularExpression uid("(?:^|[ \\t\\n])(?:mUserId|userId|U)[ \\t]*=[ \\t]*([0-9]+)\\b");
        auto u = uid.globalMatch(identity);
        while (u.hasNext()) {
            bool ok = false; const int n = u.next().captured(1).toInt(&ok);
            if (!ok) return fail("invalid-task-user"); users.insert(n);
        }
        if (users.isEmpty()) {
            auto shortId = QRegularExpression("(?:^|[ \\t])u([0-9]+)(?:[ \\t}]|$)").match(heading);
            if (shortId.hasMatch()) { bool ok = false; int n = shortId.captured(1).toInt(&ok); if (!ok) return fail("invalid-task-user"); users.insert(n); }
        }
        if (users.size() != 1) return fail(QString("task-%1-user-missing-or-conflicting").arg(heads[i].captured(1)));
        if (*users.constBegin() != activeUser) continue;
        const auto type = QRegularExpression("\\b(?:type|activityType)[ \\t]*=[ \\t]*(home|recents|dream|assistant|[2-9][0-9]*)\\b");
        if (type.match(identity).hasMatch() || QRegularExpression("\\b(?:isAvailable|inRecents)[ \\t]*=[ \\t]*false\\b").match(fields).hasMatch()
            || QRegularExpression("\\bisExcluded[ \\t]*=[ \\t]*true\\b").match(fields).hasMatch()) continue;
        const QString componentPattern = QStringLiteral(
            "(?:realActivity|mRealActivity|mActivityComponent)[ \\t]*=[ \\t]*(?:ComponentInfo)?\\{?[ \\t]*"
            "([A-Za-z][A-Za-z0-9_.]*)/[A-Za-z0-9_.$]+(?=[ \\t}\\r\\n]|$)");
        const auto component = QRegularExpression("(?:^|\\n)[ \\t]*" + componentPattern).match(fields);
        QString package;
        if (component.hasMatch()) package = component.captured(1);
        else if (info) { // Some ROMs print all RecentTaskInfo fields on one line.
            const auto inlineComponent = QRegularExpression("(?:^|[ \\t])" + componentPattern).match(heading);
            if (inlineComponent.hasMatch()) package = inlineComponent.captured(1);
        }
        if (package.isEmpty()) {
            if (QRegularExpression("(?:^|\\n)(?:realActivity|mRealActivity|mActivityComponent|intent)[ \\t]*=[ \\t]*null(?:\\s|$)").match(fields).hasMatch()) continue;
            // A task's own base/launch Intent may supply a component when its
            // realActivity field is omitted. Do NOT select topActivity or A=.
            const auto intent = QRegularExpression("(?:^|\\n)(?:intent|baseIntent)[ \\t]*=[^\\n]*?\\bcmp=([A-Za-z][A-Za-z0-9_.]*)/[A-Za-z0-9_.$]+(?=[ \\t}]|$)").match(fields);
            if (intent.hasMatch()) package = intent.captured(1);
        }
        if (!AppBinding::validPackage(package)) return fail(QString("task-%1-component-missing-or-unsupported").arg(heads[i].captured(1)));
        if (package == "android" || package == "com.android.systemui" || seen.contains(package)) continue;
        seen.insert(package); found.append(package);
    }
    *out = found; return true;
}
inline bool parse(QString text, QStringList *packages, int *user, QString *reason) {
    auto fail = [reason](const QString &s) { if (reason) *reason = s; return false; };
    if (!packages || !user || text.size() > 4 * 1024 * 1024) return fail("invalid-or-oversized-output");
    text.remove('\r');
    if (commandError(text)) return fail("android-command-error");
    const QRegularExpression beginPattern("(?:\\A|\\n)[ \\t]*QSC_USER:([0-9]+)[ \\t]*\\n");
    const auto begin = beginPattern.match(text);
    const auto end = QRegularExpression("\\nQSC_USER_END:([0-9]+)[ \\t]*\\n?\\z").match(text);
    if (!begin.hasMatch() || !end.hasMatch() || end.capturedStart() < begin.capturedEnd()) return fail("user-envelope-missing-or-truncated");
    if (beginPattern.match(text, begin.capturedEnd()).hasMatch()) return fail("duplicate-user-envelope");
    if (begin.captured(1) != end.captured(1)) return fail("android-user-changed-during-query");
    bool ok = false; const int activeUser = begin.captured(1).toInt(&ok);
    if (!ok || activeUser < 0) return fail("invalid-current-user");
    QString body = text.mid(begin.capturedEnd(), end.capturedStart() - begin.capturedEnd());
    if (!body.contains("ACTIVITY MANAGER RECENT TASKS")) return fail("recents-section-missing");
    const auto visible = QRegularExpression("(?:^|\\n)[ \\t]*Visible recent tasks[^\\n]*:").match(body);
    QString secondary;
    if (visible.hasMatch()) { secondary = body.mid(visible.capturedEnd()); body.truncate(visible.capturedStart()); }
    QStringList found; QString primaryReason;
    const bool onlyVisible = visible.hasMatch() && !body.contains("Recent #") && secondary.contains("RecentTaskInfo");
    if (onlyVisible || !records(body, false, activeUser, &found, &primaryReason)) {
        QString secondaryReason;
        if (!visible.hasMatch() || !records(secondary, true, activeUser, &found, &secondaryReason))
            return fail(primaryReason + (visible.hasMatch() ? "; visible: " + secondaryReason : QString()));
        if (reason) *reason = "visible-recent-task-info";
    } else if (reason) *reason = "recent-task-records";
    *packages = found; *user = activeUser; return true;
}
}
#endif
