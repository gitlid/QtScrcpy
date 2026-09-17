#ifndef OVERFLOWAPPROW_H
#define OVERFLOWAPPROW_H
#include <QHBoxLayout>
#include <QPushButton>
#include <QToolButton>
#include <QWidget>

// A row owns two independent click targets. Closing must never trigger launch.
class OverflowAppRow final : public QWidget {
public:
    QPushButton *launch;
    QToolButton *close;
    explicit OverflowAppRow(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName("overflowAppRow"); setFixedHeight(40); setMinimumWidth(260);
        auto *layout = new QHBoxLayout(this); layout->setContentsMargins(4, 2, 4, 2); layout->setSpacing(2);
        launch = new QPushButton(this); launch->setObjectName("switchPhoneApp");
        launch->setCheckable(true); launch->setFlat(true); launch->setMinimumWidth(0);
        launch->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
        launch->setIconSize(QSize(20, 20)); launch->setFocusPolicy(Qt::StrongFocus);
        close = new QToolButton(this); close->setObjectName("closePhoneApp");
        close->setText(QString::fromUtf8("×")); close->setFixedSize(32, 32); close->setFocusPolicy(Qt::StrongFocus);
        layout->addWidget(launch, 1); layout->addWidget(close);
        setStyleSheet(QStringLiteral(
            "#overflowAppRow { background:#24262b; }"
            "#overflowAppRow QPushButton { color:#e7e9ef; text-align:left; padding:4px 8px; border:0; background:transparent; }"
            "#overflowAppRow QPushButton:checked { background:#345779; color:white; border-radius:4px; }"
            "#overflowAppRow QPushButton:hover, #overflowAppRow QPushButton:focus { background:#444a56; border-radius:4px; }"
            "#overflowAppRow QToolButton { color:#aeb9c8; border:0; font-size:20px; padding:0; background:transparent; }"
            "#overflowAppRow QToolButton:hover, #overflowAppRow QToolButton:focus { color:white; background:#993f4b; border-radius:4px; }"
            "#overflowAppRow QToolButton:disabled, #overflowAppRow QPushButton:disabled { color:#656d79; }"));
    }
    void updateEntry(const QString &package, const QString &name, const QIcon &icon, bool current, bool ready, bool closeable) {
        setProperty("package", package); launch->setProperty("package", package); close->setProperty("package", package);
        const QString caption = fontMetrics().elidedText(name, Qt::ElideRight, qMax(140, width() - 100));
        QString escaped = caption; escaped.replace('&', "&&");
        launch->setText(escaped); launch->setIcon(icon); launch->setChecked(current); launch->setEnabled(ready);
        launch->setToolTip(name + '\n' + package); launch->setAccessibleName(name);
        close->setEnabled(closeable); close->setAccessibleName(tr("关闭 %1").arg(name));
        close->setToolTip(closeable ? tr("关闭 %1（强制停止，需确认）").arg(name) : tr("最近任务未确认、已退出或正在关闭，暂不可关闭"));
    }
};
#endif
