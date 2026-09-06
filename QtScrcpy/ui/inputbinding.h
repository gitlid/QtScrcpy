#ifndef INPUTBINDING_H
#define INPUTBINDING_H

#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaEnum>
#include <QString>

// Keep existing JSON names; compare device + numeric value, not spelling.
// BackButton, XButton1 and ExtraButton1 are aliases for the same input.
namespace InputBinding {
inline int mouseButton(const QString &name)
{
    bool ok = false;
    const int value = QMetaEnum::fromType<Qt::MouseButtons>().keyToValue(name.toLatin1().constData(), &ok);
    return ok && value > 0 && (value & (value - 1)) == 0 && value <= int(Qt::MaxMouseButton) ? value : 0;
}
inline QString mouseName(int button)
{
    switch (button) {
    case Qt::LeftButton: return QStringLiteral("LeftButton");
    case Qt::RightButton: return QStringLiteral("RightButton");
    case Qt::MiddleButton: return QStringLiteral("MiddleButton");
    case Qt::BackButton: return QStringLiteral("BackButton");
    case Qt::ForwardButton: return QStringLiteral("ForwardButton");
    default: break;
    }
    if (button <= 0 || (button & (button - 1)) != 0 || button > int(Qt::MaxMouseButton)) { return {}; }
    const char *name = QMetaEnum::fromType<Qt::MouseButtons>().valueToKey(button);
    return name ? QString::fromLatin1(name) : QString();
}
inline QString identity(const QString &name)
{
    bool ok = false;
    const int key = QMetaEnum::fromType<Qt::Key>().keyToValue(name.toLatin1().constData(), &ok);
    if (ok && key != Qt::Key_unknown) { return QStringLiteral("key:%1").arg(key); }
    const int button = mouseButton(name);
    return button ? QStringLiteral("mouse:%1").arg(button) : QString();
}
inline QString label(const QString &name)
{
    switch (mouseButton(name)) {
    case Qt::LeftButton: return QStringLiteral("鼠标左键");
    case Qt::RightButton: return QStringLiteral("鼠标右键");
    case Qt::MiddleButton: return QStringLiteral("鼠标中键（按下滚轮）");
    case Qt::BackButton: return QStringLiteral("鼠标侧键 X1（后退）");
    case Qt::ForwardButton: return QStringLiteral("鼠标侧键 X2（前进）");
    default: return name;
    }
}
// Normal-view Home/Back shortcuts must not swallow a mouse mapping toggle.
inline bool isMouseSwitch(const QString &script, Qt::MouseButton button)
{
    if (button == Qt::NoButton || script.size() > 1024 * 1024) { return false; }
    const auto root = QJsonDocument::fromJson(script.toUtf8()).object();
    return mouseButton(root.value(QStringLiteral("switchKey")).toString()) == int(button);
}
}
#endif
