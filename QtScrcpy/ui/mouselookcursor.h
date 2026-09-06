#ifndef MOUSELOOKCURSOR_H
#define MOUSELOOKCURSOR_H
#include <QCursor>
#include <QPointer>
#include <QWidget>

// Own only this video's cursor. Never modify the application override stack.
class MouseLookCursor {
public:
    ~MouseLookCursor() { release(); }
    bool active() const { return m_active; }
    void set(QWidget *widget, bool enabled) {
        if (!enabled || widget != m_widget.data()) { release(); }
        if (!enabled || !widget || m_active) { return; }
        m_widget = widget;
        m_hadCursor = widget->testAttribute(Qt::WA_SetCursor);
        m_saved = widget->cursor();
        m_active = true;
        widget->setCursor(Qt::BlankCursor);
    }
    void release() {
        if (m_active && m_widget) {
            if (m_hadCursor) { m_widget->setCursor(m_saved); }
            else { m_widget->unsetCursor(); }
        }
        m_widget.clear();
        m_active = false;
    }
private:
    QPointer<QWidget> m_widget;
    QCursor m_saved;
    bool m_active = false;
    bool m_hadCursor = false;
};
#endif
