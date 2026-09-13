#ifndef APPLABELS_H
#define APPLABELS_H
#include "appbinding.h"
#include <QHash>
#include <QRegularExpression>

namespace AppLabels {
inline bool usable(const QString &name, const QString &packageName) {
    return !name.trimmed().isEmpty() && name != packageName && name.size() <= 160
        && !name.contains(QChar::ReplacementCharacter);
}
// scrcpy wraps package names to the following line for labels >= 30 chars.
// Never infer a friendly name from a package identifier or from diagnostic text.
inline QHash<QString, QString> parse(QString output) {
    output.remove(QRegularExpression(QStringLiteral("\\x1b\\[[0-?]*[ -/]*[@-~]")));
    const QRegularExpression entry(QStringLiteral("^[*\\-]\\s+(.+)$"));
    const QRegularExpression columns(QStringLiteral("^(.+?)\\s+([A-Za-z][A-Za-z0-9_]*(?:\\.[A-Za-z0-9_]+)+)$"));
    QHash<QString, QString> labels;
    QString pending;
    for (QString line : output.split('\n')) {
        line = line.trimmed();
        const QString prefix = QStringLiteral("[server] INFO:");
        if (line.startsWith(prefix)) line = line.mid(prefix.size()).trimmed();
        const auto head = entry.match(line);
        if (head.hasMatch()) {
            pending.clear();
            const QString value = head.captured(1).trimmed();
            const auto pair = columns.match(value);
            if (pair.hasMatch() && AppBinding::validPackage(pair.captured(2))) {
                const QString name = pair.captured(1).trimmed().left(160);
                if (usable(name, pair.captured(2))) labels.insert(pair.captured(2), name);
            } else { pending = value.left(160); }
        } else if (!pending.isEmpty() && AppBinding::validPackage(line)) {
            if (usable(pending, line)) labels.insert(line, pending);
            pending.clear();
        } else if (!line.isEmpty()) { pending.clear(); }
        if (labels.size() >= 1024) break;
    }
    return labels;
}
}
#endif
