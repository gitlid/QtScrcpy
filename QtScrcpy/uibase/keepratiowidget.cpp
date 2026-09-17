#include <QResizeEvent>
#include <cmath>

#include "keepratiowidget.h"

KeepRatioWidget::KeepRatioWidget(QWidget *parent) : QWidget(parent) {}

KeepRatioWidget::~KeepRatioWidget() {}

void KeepRatioWidget::setWidget(QWidget *w)
{
    if (!w) {
        return;
    }
    w->setParent(this);
    m_subWidget = w;
}

void KeepRatioWidget::setWidthHeightRatio(float widthHeightRatio)
{
    if (fabs(m_widthHeightRatio - widthHeightRatio) < 0.000001f) {
        return;
    }
    m_widthHeightRatio = widthHeightRatio;
    adjustSubWidget();
}

void KeepRatioWidget::setFitWithinBounds(bool enabled)
{
    if (m_fitWithinBounds == enabled) return;
    m_fitWithinBounds = enabled;
    adjustSubWidget();
}

const QSize KeepRatioWidget::goodSize()
{
    if (!m_subWidget || m_widthHeightRatio < 0.0f) {
        return QSize();
    }
    return m_subWidget->size();
}

void KeepRatioWidget::resizeEvent(QResizeEvent *event)
{
    Q_UNUSED(event)
    adjustSubWidget();
}

void KeepRatioWidget::adjustSubWidget()
{
    if (!m_subWidget) {
        return;
    }

    QSize curSize = size();
    QPoint pos(0, 0);
    int width = 0;
    int height = 0;
    if (m_fitWithinBounds && m_widthHeightRatio > 0.0f) {
        width = qMin(curSize.width(), int(curSize.height() * m_widthHeightRatio));
        height = qMin(curSize.height(), int(curSize.width() / m_widthHeightRatio));
        width = qMax(0, width); height = qMax(0, height);
        pos = QPoint((curSize.width() - width) / 2, (curSize.height() - height) / 2);
    } else if (m_widthHeightRatio > 1.0f) {
        // base width
        width = curSize.width();
        height = curSize.width() / m_widthHeightRatio;
        pos.setY((curSize.height() - height) / 2);
    } else if (m_widthHeightRatio > 0.0f) {
        // base height
        height = curSize.height();
        width = curSize.height() * m_widthHeightRatio;
        pos.setX((curSize.width() - width) / 2);
    } else {
        // full widget
        height = curSize.height();
        width = curSize.width();
    }
    m_subWidget->setGeometry(pos.x(), pos.y(), width, height);
}
