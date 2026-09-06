#ifndef INPUTBINDINGFIELD_H
#define INPUTBINDINGFIELD_H

#include <QAction>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QStyle>
#include "inputbinding.h"

// Left-click only focuses the field. Choose LeftButton deliberately from
// the trailing menu; selecting an input field must never overwrite it.
class InputBindingField : public QLineEdit
{
public:
    explicit InputBindingField(QWidget *parent = nullptr) : QLineEdit(parent)
    {
        setReadOnly(true);
        setContextMenuPolicy(Qt::PreventContextMenu);
        setPlaceholderText(tr("按键绑定 / 右侧箭头选鼠标"));
        setToolTip(tr("点击此框后按键盘键、右键、中键或侧键。鼠标左键请从右侧箭头菜单选择。"));
        m_mouseMenu = new QMenu(this);
        m_mouseMenu->setObjectName("mouseButtonsMenu");
        for (int button : {int(Qt::LeftButton), int(Qt::RightButton), int(Qt::MiddleButton),
                           int(Qt::BackButton), int(Qt::ForwardButton)}) {
            const QString name = InputBinding::mouseName(button);
            auto *action = m_mouseMenu->addAction(InputBinding::label(name));
            action->setData(name);
            connect(action, &QAction::triggered, this, [this, name]() { commitBinding(name); });
        }
        auto *choose = addAction(style()->standardIcon(QStyle::SP_ArrowDown), QLineEdit::TrailingPosition);
        choose->setObjectName("chooseMouseButton");
        choose->setText(tr("选择鼠标按键"));
        choose->setToolTip(tr("选择左键、右键、中键或侧键 X1 / X2"));
        connect(choose, &QAction::triggered, this, [this]() {
            setFocus(Qt::OtherFocusReason);
            m_mouseMenu->popup(mapToGlobal(QPoint(0, height())));
        });
    }
protected:
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::ShortcutOverride) { event->accept(); return true; }
        if (event->type() == QEvent::KeyPress) {
            const auto *key = static_cast<QKeyEvent *>(event);
            if (!key->isAutoRepeat() && key->key() != Qt::Key_unknown) {
                const char *name = QMetaEnum::fromType<Qt::Key>().valueToKey(key->key());
                if (name) { commitBinding(QString::fromLatin1(name)); }
            }
            event->accept(); return true;
        }
        if (event->type() == QEvent::KeyRelease || event->type() == QEvent::ContextMenu) {
            event->accept(); return true;
        }
        if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick
            || event->type() == QEvent::MouseButtonRelease) {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() != Qt::LeftButton) {
                if (event->type() == QEvent::MouseButtonPress) {
                    commitBinding(InputBinding::mouseName(int(mouse->button())));
                }
                event->accept(); return true;
            }
        }
        return QLineEdit::event(event);
    }
private:
    void commitBinding(const QString &name)
    {
        if (!isEnabled() || InputBinding::identity(name).isEmpty()) { return; }
        setFocus(Qt::OtherFocusReason);
        setText(name);
        emit editingFinished();
    }
    QMenu *m_mouseMenu = nullptr;
};
#endif
