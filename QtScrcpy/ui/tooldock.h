#ifndef TOOLDOCK_H
#define TOOLDOCK_H
#include <QScrollArea>
#include <QScrollBar>
#include <QWheelEvent>

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
protected:
    void wheelEvent(QWheelEvent *event) override {
        const int delta = !event->pixelDelta().isNull() ? event->pixelDelta().y() : event->angleDelta().y() / 2;
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta);
        event->accept(); // Even at the limit, scrolling the tools must not scroll the phone.
    }
};
#endif
