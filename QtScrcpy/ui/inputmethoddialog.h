#ifndef INPUTMETHODDIALOG_H
#define INPUTMETHODDIALOG_H
#include <QDialog>
#include <QPlainTextEdit>
#include <QPointer>
#include <QVector>
#include "../QtScrcpyCore/include/QtScrcpyCore.h"
class AppSession;
class AppCommands;
class QLabel;
class QPushButton;
class ImeComposeEdit : public QPlainTextEdit {
    Q_OBJECT
public:
    using QPlainTextEdit::QPlainTextEdit;
    bool composing() const { return m_composing; }
signals:
    void compositionChanged();
protected:
    void inputMethodEvent(QInputMethodEvent *event) override;
private:
    bool m_composing = false;
};

// Native Qt editor owns Windows IME composition. No preedit or key events are
// forwarded to the phone; only explicit Send transmits committed Unicode.
class InputMethodDialog : public QDialog {
    Q_OBJECT
public:
    InputMethodDialog(qsc::IDevice *device, AppSession *apps, QWidget *parent = nullptr,
                      AppCommands *commands = nullptr);
    ~InputMethodDialog() override;
private:
    bool available() const;
    void refresh();
    void sendText();
    void shortcut(int which);
    void openSettings(bool physical);
    QPointer<qsc::IDevice> m_device;
    QPointer<AppSession> m_apps;
    AppCommands *m_commands;
    ImeComposeEdit *m_edit;
    QPushButton *m_send;
    QLabel *m_status;
    QVector<QPushButton *> m_shortcuts, m_settings;
    bool m_connected = true;
};
#endif
