#ifndef TOOLDOCK_H
#define TOOLDOCK_H
#include <QScrollArea>
#include <QScrollBar>
#include <QWheelEvent>
#include <QChildEvent>

// A child of the video window: no floating native window or magnetic movement.
class ToolDock : public QScrollArea {
public:
    explicit ToolDock(QWidget *parent) : QScrollArea(parent) {
        setObjectName("integratedToolDock");
        setFrameShape(QFrame::NoFrame);
        setFixedWidth(66);
        setMinimumHeight(0);
        setWidgetResizable(true);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_NoMousePropagation);
        viewport()->setAttribute(Qt::WA_NoMousePropagation);
        setStyleSheet("#integratedToolDock { background:#252525; border-left:1px solid #424242; }"
                      "#integratedToolDock QScrollBar:vertical { width:10px; }");
    }
    void setWidget(QWidget *content) {
        QScrollArea::setWidget(content);
        watchContent(content);
    }
protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() == QEvent::ChildPolished) {
            auto *child = qobject_cast<QWidget *>(static_cast<QChildEvent *>(event)->child());
            if (child && !child->isWindow()) watchContent(child);
        }
        if (event->type() == QEvent::Wheel) {
            auto *target = qobject_cast<QWidget *>(watched);
            if (target && widget() && target->window() == window()
                && (target == widget() || widget()->isAncestorOf(target))) {
                // ToolForm stops mouse propagation to protect the phone.
                // Handle wheel events before a button or that boundary eats them.
                wheelEvent(static_cast<QWheelEvent *>(event));
                return true;
            }
        }
        return QScrollArea::eventFilter(watched, event);
    }
    void wheelEvent(QWheelEvent *event) override {
        const int delta = !event->pixelDelta().isNull() ? event->pixelDelta().y() : event->angleDelta().y() / 2;
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta);
        event->accept(); // Even at the limit, scrolling the tools must not scroll the phone.
    }
private:
    void watchContent(QWidget *content) {
        if (!content || content->isWindow()) return;
        content->installEventFilter(this);
        for (auto *child : content->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly))
            watchContent(child);
    }
};
#endif
