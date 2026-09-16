#ifndef APPBAR_H
#define APPBAR_H
#include <QWidget>
#include <QPointer>
class AppSession;
class QLabel;
class QTabBar;
class QToolButton;
class QMenu;
class AppBar : public QWidget {
    Q_OBJECT
public:
    explicit AppBar(AppSession *session, QWidget *parent = nullptr);
signals:
    void editKeymap();
    void openMacros();
protected:
    void wheelEvent(QWheelEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    void refresh();
    void chooseApp();
    void scheduleOverflow();
    void updateOverflow();
    void populateOverflow();
    QPointer<AppSession> m_session;
    QTabBar *m_tabs;
    QLabel *m_status;
    QToolButton *m_add, *m_keys, *m_macros, *m_stop, *m_more;
    QMenu *m_overflow;
    bool m_overflowQueued = false;
};
#endif
