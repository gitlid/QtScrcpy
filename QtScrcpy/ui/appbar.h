#ifndef APPBAR_H
#define APPBAR_H
#include <QWidget>
#include <QPointer>
class AppSession;
class QLabel;
class QTabBar;
class QToolButton;
class AppBar : public QWidget {
    Q_OBJECT
public:
    explicit AppBar(AppSession *session, QWidget *parent = nullptr);
signals:
    void editKeymap();
    void openMacros();
protected:
    void wheelEvent(QWheelEvent *event) override;
private:
    void refresh();
    void chooseApp();
    QPointer<AppSession> m_session;
    QTabBar *m_tabs;
    QLabel *m_status;
    QToolButton *m_add, *m_keys, *m_macros, *m_stop;
};
#endif
