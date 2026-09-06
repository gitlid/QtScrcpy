#ifndef KEYMAPEDITOR_H
#define KEYMAPEDITOR_H
#include <QDialog>
#include <QPixmap>
#include <QPointer>
#include "keymapdocument.h"
class QGraphicsScene;class QGraphicsView;class QListWidget;class QLineEdit;class QDoubleSpinBox;class QLabel;
class KeymapEditor : public QDialog {
 Q_OBJECT
public:
 KeymapEditor(const QPixmap &frame,const QString &script,QWidget *parent=nullptr);
 QString script()const{return QString::fromUtf8(QJsonDocument(m_document.root).toJson());}
 QString savedPath()const{return m_path;}
private:
 void rebuild();void properties();void updateNode();void load();bool save();void apply();void reject()override;
 KeymapDocument m_document;QPixmap m_frame;QString m_path;bool m_dirty=false;bool m_updating=false;
 QGraphicsScene*m_scene;QGraphicsView*m_view;QListWidget*m_list;
 QVector<QLineEdit*>m_keys;QLineEdit*m_switch;QDoubleSpinBox*m_range;QLabel*m_error;
};
#endif
