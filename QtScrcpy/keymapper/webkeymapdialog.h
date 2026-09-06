#pragma once
#include <QDialog>
#include <QImage>
#include <QPointer>
#include <QVariantMap>
#include <functional>
class QLabel;
class QPushButton;
class QWebEngineView;
class QWebEnginePage;
class QWebEngineProfile;

// This bridge cannot access arbitrary files, run commands or control a phone.
class KeymapperBridge : public QObject {
    Q_OBJECT
public:
    explicit KeymapperBridge(QVariantMap payload, QObject *parent=nullptr):QObject(parent),m_payload(payload){}
    Q_INVOKABLE QVariantMap bootstrap() const { return m_payload; }
    Q_INVOKABLE void changed() { emit contentChanged(); }
    Q_INVOKABLE void editorReady() { emit ready(); }
    Q_INVOKABLE void editorFailed(const QString &message) { emit failed(message.left(1024)); }
    void setConfig(const QString &config) { m_payload["config"]=config; }
signals:
    void contentChanged();
    void ready();
    void failed(const QString &message);
private:
    QVariantMap m_payload;
};
class WebKeymapDialog : public QDialog {
    Q_OBJECT
public:
    static void registerScheme();
    WebKeymapDialog(const QImage &image,const QString &script,QWidget *parent=nullptr);
    ~WebKeymapDialog() override;
    QString script() const { return m_script; }
    bool isReady() const { return m_ready; }
    QWebEnginePage *page() const;
    void readConfiguration(std::function<void(QString)> callback);
    void invalidateSession();
signals:
    void editorReady();
protected:
    void reject() override;
    void closeEvent(QCloseEvent *event) override;
private:
    bool mayDiscard();
    void importConfiguration();
    void saveConfiguration(bool apply);
    void error(const QString &message);
    QWebEngineView *m_view=nullptr;
    QWebEngineProfile *m_profile=nullptr;
    KeymapperBridge *m_bridge=nullptr;
    QLabel *m_status=nullptr;
    QPushButton *m_save=nullptr;
    QPushButton *m_apply=nullptr;
    QString m_script,m_path;
    bool m_ready=false,m_dirty=false,m_validSession=true,m_requestPending=false;
};
