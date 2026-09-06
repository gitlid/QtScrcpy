#include "webkeymapdialog.h"
#include "../ui/keymapdocument.h"
#include <QBuffer>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QTimer>
#include <QVBoxLayout>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInfo>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>
#include <QWebEngineUrlSchemeHandler>
#include <QWebEngineView>

static void initializeKeymapperAssets(){Q_INIT_RESOURCE(keymapper_assets);}
namespace {
const char schemeName[]="qsc-keymapper";
bool editorUrl(const QUrl &url){return url.scheme()==QLatin1String(schemeName)&&url.host()==QLatin1String("editor");}
class LocalAssets:public QWebEngineUrlSchemeHandler {
public:
    using QWebEngineUrlSchemeHandler::QWebEngineUrlSchemeHandler;
    void requestStarted(QWebEngineUrlRequestJob *job) override {
        const QUrl url=job->requestUrl();const QString path=url.path();
        if(!editorUrl(url)||job->requestMethod()!="GET"||path.contains("..")||path.contains('\\')){job->fail(QWebEngineUrlRequestJob::RequestDenied);return;}
        auto *file=new QFile(QStringLiteral(":/keymapper")+path,job);
        if(!file->open(QIODevice::ReadOnly)){job->fail(QWebEngineUrlRequestJob::UrlNotFound);return;}
        QByteArray mime="text/plain";
        if(path.endsWith(".html"))mime="text/html";
        else if(path.endsWith(".js"))mime="text/javascript";
        else if(path.endsWith(".css"))mime="text/css";
        else if(path.endsWith(".svg"))mime="image/svg+xml";
        job->reply(mime,file);
    }
};
class LocalOnly:public QWebEngineUrlRequestInterceptor {
public:
    using QWebEngineUrlRequestInterceptor::QWebEngineUrlRequestInterceptor;
    void interceptRequest(QWebEngineUrlRequestInfo &info) override {
        const auto url=info.requestUrl();
        const bool allowed=editorUrl(url)||url==QUrl("qrc:///qtwebchannel/qwebchannel.js")||url.scheme()=="data"||url.scheme()=="blob";
        if(!allowed)info.block(true);
    }
};
class EditorPage:public QWebEnginePage {
public:
    EditorPage(QWebEngineProfile *profile,QObject *parent):QWebEnginePage(profile,parent){}
protected:
    bool acceptNavigationRequest(const QUrl &url,NavigationType,bool isMainFrame) override{return !isMainFrame||editorUrl(url);}
    QWebEnginePage *createWindow(WebWindowType) override{return nullptr;}
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel,const QString &message,int line,const QString &source) override{
        qWarning().noquote()<<"Keymapper JS:"<<message<<line<<source;
    }
};
QString jsString(const QString &value){
    const QByteArray array=QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(array.mid(1,array.size()-2));
}
}
void WebKeymapDialog::registerScheme(){
    QWebEngineUrlScheme scheme(schemeName);scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme|QWebEngineUrlScheme::LocalScheme|QWebEngineUrlScheme::LocalAccessAllowed|QWebEngineUrlScheme::CorsEnabled);
    QWebEngineUrlScheme::registerScheme(scheme);
}
WebKeymapDialog::WebKeymapDialog(const QImage &image,const QString &original,QWidget *parent):QDialog(parent){
    initializeKeymapperAssets();setWindowTitle(tr("ScrcpyKeyMapper — QtScrcpy 0.3.0-rc.1"));resize(1200,850);setWindowModality(Qt::ApplicationModal);
    KeymapDocument document;QString validation;
    if(!original.trimmed().isEmpty()&&!document.parse(original.toUtf8(),&validation))m_validSession=false;
    m_script=QString::fromUtf8(QJsonDocument(document.root).toJson());
    QImage preview=image;if(preview.width()>2560||preview.height()>2560)preview=preview.scaled(2560,2560,Qt::KeepAspectRatio,Qt::SmoothTransformation);
    QByteArray png;QBuffer buffer(&png);buffer.open(QIODevice::WriteOnly);preview.save(&buffer,"PNG");
    m_bridge=new KeymapperBridge({{"config",m_script},{"background",QString("data:image/png;base64,")+QString::fromLatin1(png.toBase64())}},this);
    m_profile=new QWebEngineProfile(this);
    m_profile->setHttpCacheType(QWebEngineProfile::MemoryHttpCache);m_profile->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);
    m_profile->installUrlSchemeHandler(schemeName,new LocalAssets(m_profile));m_profile->setUrlRequestInterceptor(new LocalOnly(m_profile));
    m_view=new QWebEngineView(this);m_view->setContextMenuPolicy(Qt::NoContextMenu);
    auto *page=new EditorPage(m_profile,m_view);m_view->setPage(page);
    auto *channel=new QWebChannel(page);channel->registerObject("host",m_bridge);page->setWebChannel(channel);
    auto *settings=page->settings();
    settings->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows,false);
    settings->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard,false);
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls,false);
    settings->setAttribute(QWebEngineSettings::PluginsEnabled,false);
    connect(page,&QWebEnginePage::featurePermissionRequested,page,[page](const QUrl &url,QWebEnginePage::Feature feature){page->setFeaturePermission(url,feature,QWebEnginePage::PermissionDeniedByUser);});
    m_status=new QLabel(tr("正在加载离线键位编辑器…"),this);m_status->setWordWrap(true);m_status->setTextFormat(Qt::PlainText);
    auto *load=new QPushButton(tr("导入 JSON"),this);m_save=new QPushButton(tr("保存"),this);m_apply=new QPushButton(tr("保存并应用"),this);
    auto *cancel=new QPushButton(tr("取消"),this);m_save->setEnabled(false);m_apply->setEnabled(false);
    auto *row=new QHBoxLayout;row->addWidget(load);row->addStretch();row->addWidget(m_save);row->addWidget(m_apply);row->addWidget(cancel);
    auto *layout=new QVBoxLayout(this);layout->addWidget(m_view,1);layout->addWidget(m_status);layout->addLayout(row);
    connect(load,&QPushButton::clicked,this,&WebKeymapDialog::importConfiguration);
    connect(m_save,&QPushButton::clicked,this,[this]{saveConfiguration(false);});
    connect(m_apply,&QPushButton::clicked,this,[this]{saveConfiguration(true);});
    connect(cancel,&QPushButton::clicked,this,&WebKeymapDialog::reject);
    connect(m_bridge,&KeymapperBridge::contentChanged,this,[this]{if(m_ready)m_dirty=true;});
    connect(m_bridge,&KeymapperBridge::failed,this,&WebKeymapDialog::error);
    connect(m_bridge,&KeymapperBridge::ready,this,[this,validation]{
        m_ready=true;m_dirty=false;m_save->setEnabled(m_validSession);m_apply->setEnabled(m_validSession);
        m_status->setText(validation.isEmpty()?tr("编辑画面是快照，不向手机发送操作。保存并应用后，回到投屏按映射开关键启用。"):
            tr("原方案未通过校验，已禁止覆盖/应用：%1。请导入有效配置。").arg(validation));
        emit editorReady();
    });
    connect(m_view,&QWebEngineView::loadFinished,this,[this](bool ok){if(!ok)error(tr("离线编辑器加载失败，请检查运行包是否完整。"));});
    QTimer::singleShot(20000,this,[this]{if(!m_ready)error(tr("编辑器启动超时。请保留日志，未修改手机键位配置。"));});
    m_view->setUrl(QUrl("qsc-keymapper://editor/index.html"));
}
WebKeymapDialog::~WebKeymapDialog(){delete m_view;m_view=nullptr;delete m_profile;m_profile=nullptr;}
QWebEnginePage *WebKeymapDialog::page()const{return m_view?m_view->page():nullptr;}
void WebKeymapDialog::error(const QString &message){m_status->setText(message);}
void WebKeymapDialog::readConfiguration(std::function<void(QString)> callback){
    if(!m_ready||!page()){callback(QString());return;}
    QPointer<WebKeymapDialog> self(this);
    page()->runJavaScript("(()=>{try{return JSON.stringify(qscEditor.exportConfig());}catch(e){return '';}})()",[self,callback](const QVariant &value){if(self)callback(value.toString());});
}
bool WebKeymapDialog::mayDiscard(){return !m_dirty||QMessageBox::question(this,tr("未保存"),tr("丢弃本次未保存的键位修改？"),QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Cancel)==QMessageBox::Discard;}
void WebKeymapDialog::reject(){if(mayDiscard())QDialog::reject();}
void WebKeymapDialog::closeEvent(QCloseEvent *e){if(mayDiscard())e->accept();else e->ignore();}
void WebKeymapDialog::invalidateSession(){m_validSession=false;QDialog::done(QDialog::Rejected);}
void WebKeymapDialog::importConfiguration(){
    if(!m_ready||m_requestPending||!mayDiscard())return;
    const QString path=QFileDialog::getOpenFileName(this,tr("导入键位方案"),QCoreApplication::applicationDirPath()+"/keymap",tr("JSON (*.json)"));
    if(path.isEmpty())return;
    QFile file(path);KeymapDocument document;QString message;
    if(!file.open(QIODevice::ReadOnly)||file.size()>1024*1024||!document.parse(file.readAll(),&message)){error(tr("导入失败，当前编辑保留：%1").arg(message));return;}
    const QString config=QString::fromUtf8(QJsonDocument(document.root).toJson());m_requestPending=true;QPointer<WebKeymapDialog> self(this);
    page()->runJavaScript("(()=>{try{qscEditor.importConfig(JSON.parse("+jsString(config)+"));return '';}catch(e){return String(e);}})()",[self,path,config](const QVariant &value){
        if(!self)return;self->m_requestPending=false;if(!value.toString().isEmpty()){self->error(value.toString());return;}
        self->m_path=path;self->m_script=config;self->m_dirty=false;self->m_validSession=true;self->m_save->setEnabled(true);self->m_apply->setEnabled(true);
    });
}
void WebKeymapDialog::saveConfiguration(bool apply){
    if(!m_ready||!m_validSession||m_requestPending)return;m_requestPending=true;QPointer<WebKeymapDialog> self(this);
    readConfiguration([self,apply](QString text){
        if(!self)return;self->m_requestPending=false;KeymapDocument document;QString message;
        if(text.isEmpty()||!document.parse(text.toUtf8(),&message)){self->error(tr("配置未保存：%1").arg(message.isEmpty()?tr("编辑器导出失败；检查是否添加了多个视角节点。"):message));return;}
        QString dir=QCoreApplication::applicationDirPath()+"/keymap";QDir().mkpath(dir);
        const QString path=QFileDialog::getSaveFileName(self,tr("保存键位方案"),self->m_path.isEmpty()?dir+"/my-keymap.json":self->m_path,tr("JSON (*.json)"));
        if(!self||path.isEmpty()||!self->m_validSession)return;
        const QByteArray data=QJsonDocument(document.root).toJson();QSaveFile file(path);
        if(!file.open(QIODevice::WriteOnly)||file.write(data)!=data.size()||!file.commit()){self->error(tr("保存失败，未应用到手机。"));return;}
        self->m_script=QString::fromUtf8(data);self->m_path=path;self->m_dirty=false;self->m_status->setText(tr("已保存到 %1").arg(path));if(apply)self->accept();
    });
}
