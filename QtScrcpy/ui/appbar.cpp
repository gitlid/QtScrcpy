#include "appbar.h"
#include "appsession.h"
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QRegion>
#include <QSignalBlocker>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace {
QString literalCaption(QString text) { return text.replace('&', "&&"); }
QIcon badge(const QString &label, const QString &packageName) {
    QPixmap image(40, 40); image.fill(Qt::transparent);
    QPainter painter(&image); painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor::fromHsv(int(qHash(packageName) % 360), 120, 190)); painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(1, 1, 38, 38, 10, 10);
    QFont font; font.setPixelSize(24); font.setBold(true); painter.setFont(font); painter.setPen(Qt::white);
    painter.drawText(image.rect(), Qt::AlignCenter, label.left(1).toUpper());
    return QIcon(image);
}
}
AppBar::AppBar(AppSession *session, QWidget *parent) : QWidget(parent), m_session(session) {
    setObjectName("applicationBar"); setFixedHeight(66);
    setAttribute(Qt::WA_NoMousePropagation);
    QFont font = this->font();
#ifdef Q_OS_WIN
    font.setFamily(QStringLiteral("Microsoft YaHei UI"));
#endif
    font.setPointSize(10); setFont(font);
    setStyleSheet(QStringLiteral(
        "#applicationBar { background:#24262b; color:#e7e9ef; }"
        "#applicationBar QTabBar::tab { background:#24262b; color:#aeb3be; padding:7px 12px; min-width:70px; max-width:180px; border:0; }"
        "#applicationBar QTabBar::tab:selected { background:#393e49; color:white; border-bottom:2px solid #66a9ff; }"
        "#applicationBar QTabBar::tab:hover { background:#333741; }"
        "#applicationBar QToolButton { color:#e7e9ef; background:transparent; border:0; padding:5px 7px; }"
        "#applicationBar QToolButton:hover { background:#444a56; border-radius:4px; }"
        "#applicationBar QToolButton:disabled { color:#6e737e; }"
        "#applicationBar QLabel { color:#aeb9c8; background:transparent; font-size:11px; }"
        "#applicationBar QToolButton#stopAppMacro { color:#ff9595; }"));
    m_tabs = new QTabBar(this); m_tabs->setObjectName("phoneAppTabs");
    m_tabs->setExpanding(false); m_tabs->setUsesScrollButtons(true); m_tabs->setElideMode(Qt::ElideRight);
    m_tabs->setTabsClosable(true); m_tabs->setFocusPolicy(Qt::NoFocus); m_tabs->setIconSize(QSize(19, 19));
    // A long catalog must not force the whole video window wider than its screen.
    m_tabs->setMinimumWidth(0); m_tabs->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_tabs->installEventFilter(this);
    auto button = [this](const QString &text, const QString &tip) {
        auto *b = new QToolButton(this); b->setText(text); b->setToolTip(tip); b->setFocusPolicy(Qt::NoFocus); return b;
    };
    m_more = button(QStringLiteral(">"), tr("显示未完整显示的应用标签"));
    m_more->setObjectName("morePhoneApps"); m_more->setAccessibleName(tr("更多应用"));
    m_more->setFixedWidth(28); m_more->hide();
    m_overflow = new QMenu(this); m_overflow->setObjectName("hiddenPhoneApps");
    m_more->setMenu(m_overflow); m_more->setPopupMode(QToolButton::InstantPopup);
    connect(m_overflow, &QMenu::aboutToShow, this, &AppBar::populateOverflow);
    m_add = button(QStringLiteral("+"), tr("添加手机应用标签")); m_add->setObjectName("addPhoneApp");
    m_keys = button(tr("键位"), tr("编辑当前应用的按键映射"));
    m_macros = button(tr("操作"), tr("录制、载入或启动绑定应用的预制操作"));
    m_stop = button(tr("■ 停止"), tr("停止预制操作和自动切回应用")); m_stop->setObjectName("stopAppMacro");
    auto *row = new QHBoxLayout; row->setContentsMargins(4, 0, 4, 0); row->setSpacing(2);
    row->addWidget(m_tabs, 1); row->addWidget(m_more); row->addWidget(m_add);
    row->addWidget(m_keys); row->addWidget(m_macros); row->addWidget(m_stop);
    m_status = new QLabel(session->status(), this); m_status->setTextFormat(Qt::PlainText); m_status->setMargin(3);
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    layout->addLayout(row); layout->addWidget(m_status);
    connect(m_tabs, &QTabBar::tabBarClicked, this, [this](int index) {
        if (m_session && index >= 0) {
            m_session->activate(m_tabs->tabData(index).toString());
            QTimer::singleShot(0, this, &AppBar::refresh);
        }
    });
    connect(m_tabs, &QTabBar::currentChanged, this, [this](int) { scheduleOverflow(); });
    connect(m_tabs, &QTabBar::tabCloseRequested, this, [this](int index) {
        if (m_session && index > 0) m_session->closeTab(m_tabs->tabData(index).toString());
    });
    connect(m_add, &QToolButton::clicked, this, &AppBar::chooseApp);
    connect(m_keys, &QToolButton::clicked, this, &AppBar::editKeymap);
    connect(m_macros, &QToolButton::clicked, this, &AppBar::openMacros);
    connect(m_stop, &QToolButton::clicked, session, &AppSession::stopMacro);
    connect(session, &AppSession::appsChanged, this, &AppBar::refresh);
    connect(session, &AppSession::foregroundChanged, this, &AppBar::refresh);
    connect(session, &AppSession::lockChanged, this, &AppBar::refresh);
    connect(session, &AppSession::statusChanged, this, [this](const QString &text) { m_status->setText(text); m_status->setToolTip(text); });
    refresh();
}
void AppBar::wheelEvent(QWheelEvent *event) { event->accept(); }
bool AppBar::eventFilter(QObject *object, QEvent *event) {
    switch (event->type()) {
    case QEvent::Resize: case QEvent::Show: case QEvent::Hide: case QEvent::LayoutRequest:
    case QEvent::FontChange: case QEvent::StyleChange: case QEvent::MouseButtonRelease: case QEvent::Wheel:
        scheduleOverflow(); break;
    default: break;
    }
    return QWidget::eventFilter(object, event);
}
void AppBar::scheduleOverflow() {
    if (m_overflowQueued) return;
    m_overflowQueued = true;
    QTimer::singleShot(0, this, [this] { m_overflowQueued = false; updateOverflow(); });
}
void AppBar::updateOverflow() {
    // Add back the button's reserved space to avoid show/hide oscillation.
    const int availableWithoutButton = m_tabs->width() + (!m_more->isHidden() ? m_more->width() + 2 : 0);
    const bool overflowing = m_tabs->sizeHint().width() > availableWithoutButton;
    m_more->setVisible(overflowing);
    m_more->setEnabled(m_session && m_session->ready());
    for (auto *scroll : m_tabs->findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly))
        scroll->installEventFilter(this);
    if (!m_overflow->isVisible()) populateOverflow();
}
void AppBar::populateOverflow() {
    m_overflow->clear();
    if (!m_session) return;
    QRegion visible(m_tabs->rect());
    for (auto *scroll : m_tabs->findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly)) {
        if (scroll->isVisible()) visible = visible.subtracted(QRegion(scroll->geometry()));
    }
    int count = 0;
    for (int index = 1; index < m_tabs->count(); ++index) {
        const QString packageName = m_tabs->tabData(index).toString();
        const QString name = m_session->label(packageName);
        const QRect bounds = m_tabs->tabRect(index);
        const int textWidth = m_tabs->fontMetrics().horizontalAdvance(name) + 24 + m_tabs->iconSize().width() + 24;
        const bool clipped = bounds.isEmpty() || !QRegion(bounds).subtracted(visible).isEmpty() || textWidth > bounds.width();
        if (!clipped) continue;
        auto *action = m_overflow->addAction(badge(name, packageName), literalCaption(name));
        action->setData(packageName); action->setToolTip(name + '\n' + packageName);
        action->setCheckable(true); action->setChecked(packageName == m_session->foreground());
        action->setEnabled(m_session->ready());
        const QPointer<AppSession> session = m_session;
        connect(action, &QAction::triggered, this, [session, packageName] { if (session) session->activate(packageName); });
        ++count;
    }
    if (!count) m_overflow->addAction(tr("没有隐藏的应用"))->setEnabled(false);
    m_more->setToolTip(tr("更多应用：%1 个标签未完整显示").arg(count));
}
void AppBar::refresh() {
    if (!m_session) return;
    const QSignalBlocker blocker(m_tabs);
    while (m_tabs->count()) m_tabs->removeTab(0);
    m_tabs->addTab(tr("手机桌面")); m_tabs->setTabData(0, QString());
    m_tabs->setTabButton(0, QTabBar::RightSide, nullptr); m_tabs->setTabButton(0, QTabBar::LeftSide, nullptr);
    int current = -1;
    for (const auto &packageName : m_session->tabs()) {
        const auto name = m_session->label(packageName);
        const int index = m_tabs->addTab(badge(name, packageName), literalCaption(name));
        m_tabs->setTabData(index, packageName); m_tabs->setTabToolTip(index, name + '\n' + packageName);
        if (packageName == m_session->foreground()) current = index;
    }
    m_tabs->setCurrentIndex(current);
    // Guarded macro playback does not disable ordinary tab/menu navigation.
    m_tabs->setEnabled(m_session->ready()); m_add->setEnabled(m_session->ready()); m_macros->setEnabled(m_session->ready());
    m_keys->setEnabled(m_session->ready() && !m_session->locked() && !m_session->foreground().isEmpty());
    m_stop->setVisible(m_session->locked()); scheduleOverflow();
}
void AppBar::chooseApp() {
    const QPointer<AppSession> session = m_session;
    if (!session) return;
    QDialog picker; picker.setWindowModality(Qt::ApplicationModal);
    picker.setWindowTitle(tr("添加应用标签")); picker.resize(480, 520);
    auto *search = new QLineEdit(&picker); search->setPlaceholderText(tr("搜索应用名称或包名"));
    auto *list = new QListWidget(&picker);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel, &picker);
    buttons->button(QDialogButtonBox::Open)->setText(tr("打开应用"));
    auto *refreshButton = buttons->addButton(tr("刷新名称"), QDialogButtonBox::ActionRole);
    auto *layout = new QVBoxLayout(&picker); layout->addWidget(search); layout->addWidget(list, 1); layout->addWidget(buttons);
    auto refreshList = [session, list, search] {
        const QString selected = list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString() : QString();
        list->clear(); if (!session) return;
        for (const auto &app : session->apps()) {
            if (!(app.label + app.packageName).contains(search->text(), Qt::CaseInsensitive)) continue;
            auto *item = new QListWidgetItem(badge(app.label, app.packageName), app.label + '\n' + app.packageName, list);
            item->setData(Qt::UserRole, app.packageName);
            if (app.packageName == selected) list->setCurrentItem(item);
        }
        if (!list->currentItem() && list->count()) list->setCurrentRow(0);
    };
    connect(search, &QLineEdit::textChanged, &picker, refreshList);
    connect(session, &AppSession::appsChanged, &picker, refreshList);
    connect(session, &AppSession::appsChanged, &picker, [session, &picker] { if (!session || !session->ready()) picker.reject(); });
    connect(session, &QObject::destroyed, &picker, &QDialog::reject);
    connect(refreshButton, &QPushButton::clicked, session, &AppSession::refreshApps);
    connect(buttons, &QDialogButtonBox::accepted, &picker, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &picker, &QDialog::reject);
    connect(list, &QListWidget::itemDoubleClicked, &picker, [&picker](QListWidgetItem *) { picker.accept(); });
    refreshList();
    if (picker.exec() == QDialog::Accepted && list->currentItem() && session)
        session->activate(list->currentItem()->data(Qt::UserRole).toString());
}
