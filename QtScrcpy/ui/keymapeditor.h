#ifndef KEYMAPEDITOR_H
#define KEYMAPEDITOR_H
#include <QDialog>
#include <QPixmap>
#include <QPointer>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QTimer>
#include "keymapdocument.h"
class QListWidget;class QLineEdit;class QDoubleSpinBox;class QLabel;class QCheckBox;
class KeymapEditor : public QDialog {
 Q_OBJECT
public:
 KeymapEditor(const QPixmap &frame,const QString &script,QWidget *parent=nullptr);
 QString script()const{return QString::fromUtf8(QJsonDocument(m_document.root).toJson());}
 QString savedPath()const{return m_path;}
protected:
 void showEvent(QShowEvent *event) override {
    QDialog::showEvent(event);
    // Constructor-time widget sizes are not final. Fit only after layout.
    if(m_view){m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);}
    scheduleFit();
 }
 void resizeEvent(QResizeEvent *event) override { QDialog::resizeEvent(event);scheduleFit(); }
private:
 void scheduleFit(){QTimer::singleShot(0,this,[this](){if(m_view&&m_scene&&!m_scene->sceneRect().isEmpty())m_view->fitInView(m_scene->sceneRect(),Qt::KeepAspectRatio);});}
 void refreshMouseLook();
 void rebuild();void properties();void updateNode();void load();bool save();void apply();void reject()override;
 KeymapDocument m_document;QPixmap m_frame;QString m_path;bool m_dirty=false;bool m_updating=false;
 QGraphicsScene*m_scene=nullptr;QGraphicsView*m_view=nullptr;QListWidget*m_list=nullptr;
 QCheckBox*m_mouseLook=nullptr;
 QVector<QLineEdit*>m_keys;QLineEdit*m_switch=nullptr;QDoubleSpinBox*m_range=nullptr;QLabel*m_error=nullptr;
};
#endif
