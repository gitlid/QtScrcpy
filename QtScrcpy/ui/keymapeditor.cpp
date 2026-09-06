#include "keymapeditor.h"
#include "inputbindingfield.h"
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGraphicsEllipseItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QDoubleSpinBox>
#include <QVBoxLayout>
#include <QCoreApplication>
#include <functional>
namespace {
class Marker:public QGraphicsEllipseItem {
public:
 std::function<void(QPointF)>changed;
 explicit Marker(const QString&label,qreal radius):QGraphicsEllipseItem(-radius,-radius,2*radius,2*radius){
  setFlags(ItemIsMovable|ItemIsSelectable|ItemSendsGeometryChanges);setBrush(QColor(0,175,230,160));setPen(QPen(Qt::white,2));
  auto*t=new QGraphicsTextItem(label,this);t->setDefaultTextColor(Qt::white);t->setScale(qMax(1.0,radius/24.));t->setPos(-radius,-radius);t->setAcceptedMouseButtons(Qt::NoButton);
 }
protected:
 QVariant itemChange(GraphicsItemChange change,const QVariant&value)override{
  if(change==ItemPositionChange&&scene()){auto p=value.toPointF();auto r=scene()->sceneRect();p.setX(qBound(0.,p.x(),r.width()-1));p.setY(qBound(0.,p.y(),r.height()-1));return p;}
  if(change==ItemPositionHasChanged&&changed)changed(pos());return QGraphicsEllipseItem::itemChange(change,value);
 }
};
}
KeymapEditor::KeymapEditor(const QPixmap&frame,const QString&script,QWidget*parent):QDialog(parent),m_frame(frame){
 setWindowTitle(tr("可视化键鼠设置 — 点击、WASD、滑动"));resize(1050,760);setWindowModality(Qt::ApplicationModal);m_frame.setDevicePixelRatio(1);
 m_scene=new QGraphicsScene(this);m_view=new QGraphicsView(m_scene,this);m_view->setMinimumSize(450,400);
 m_list=new QListWidget(this);m_list->setObjectName("mappingNodes");auto*right=new QVBoxLayout;right->addWidget(m_list);auto*row=new QHBoxLayout;
 for(const auto&p:QVector<QPair<QString,QString>>{{tr("点击"),"KMT_CLICK"},{tr("WASD"),"KMT_STEER_WHEEL"},{tr("滑动"),"KMT_DRAG"}}){auto*b=new QPushButton(p.first,this);row->addWidget(b);connect(b,&QPushButton::clicked,this,[this,p](){int i=m_document.add(p.second);m_dirty=true;rebuild();m_list->setCurrentRow(i);});}
 right->addLayout(row);auto*remove=new QPushButton(tr("删除选中控件"),this);right->addWidget(remove);
 connect(remove,&QPushButton::clicked,this,[this](){int i=m_list->currentRow();auto a=m_document.nodes();if(i<0||i>=a.size())return;a.removeAt(i);m_document.root["keyMapNodes"]=a;m_dirty=true;rebuild();});
 auto*form=new QFormLayout;m_switch=new InputBindingField(this);m_switch->setObjectName("mappingSwitchKey");form->addRow(tr("开关映射键"),m_switch);
 for(int i=0;i<4;++i){auto*k=new InputBindingField(this);k->setObjectName(QString("mappingBinding%1").arg(i));m_keys.append(k);form->addRow(i==0?tr("按键／左"):i==1?tr("右"):i==2?tr("上"):tr("下"),k);connect(k,&QLineEdit::editingFinished,this,&KeymapEditor::updateNode);}
 connect(m_switch,&QLineEdit::editingFinished,this,[this](){m_document.root["switchKey"]=m_switch->text();m_dirty=true;});
 m_range=new QDoubleSpinBox(this);m_range->setRange(.001,.4);m_range->setDecimals(3);m_range->setSingleStep(.01);form->addRow(tr("摇杆范围（相对宽高）"),m_range);
 connect(m_range,QOverload<double>::of(&QDoubleSpinBox::valueChanged),this,[this](double){updateNode();});right->addLayout(form);
 m_error=new QLabel(this);m_error->setWordWrap(true);m_error->setTextFormat(Qt::PlainText);right->addWidget(m_error);
 auto*hint=new QLabel(tr("绑定框右侧箭头可选鼠标左键、右键、中键和侧键 X1 / X2；也可在框内按键或按侧键绑定。\n单击绑定框只选中，不绑定左键。拖动画面圆点调整位置；滑动有起点和终点。\n编辑画面为快照，不向手机发送点击。导入的其他映射保留原始字段。\n保存并应用后，回到投屏按开关映射键启用；再次按下返回普通/UHID 输入。"),this);hint->setWordWrap(true);right->addWidget(hint);
 auto*buttons=new QHBoxLayout;
 for(const QString&s:{tr("导入"),tr("保存"),tr("保存并应用"),tr("取消")}){
  auto*b=new QPushButton(s,this);buttons->addWidget(b);
  if(s==tr("导入"))connect(b,&QPushButton::clicked,this,&KeymapEditor::load);
  else if(s==tr("保存"))connect(b,&QPushButton::clicked,this,[this](){save();});
  else if(s==tr("保存并应用"))connect(b,&QPushButton::clicked,this,&KeymapEditor::apply);
  else connect(b,&QPushButton::clicked,this,&KeymapEditor::reject);
 }
 auto*body=new QHBoxLayout;body->addWidget(m_view,3);body->addLayout(right,2);auto*all=new QVBoxLayout(this);all->addLayout(body);all->addLayout(buttons);
 connect(m_list,&QListWidget::currentRowChanged,this,[this](int){properties();});
 if(!script.trimmed().isEmpty()){QString e;if(!m_document.parse(script.toUtf8(),&e))m_error->setText(tr("当前映射未导入：%1。原文件不会被覆盖。").arg(e));}
 m_switch->setText(m_document.root["switchKey"].toString());rebuild();
}
void KeymapEditor::rebuild(){
 int selected=m_list->currentRow();m_updating=true;m_list->clear();for(const auto&v:m_document.nodes()){auto n=v.toObject();m_list->addItem(n["type"].toString()+"  "+InputBinding::label(n["key"].toString()));}
 m_updating=false;if(m_list->count())m_list->setCurrentRow(qBound(0,selected,m_list->count()-1));else properties();
}
void KeymapEditor::properties(){
 if(m_updating)return;m_updating=true;m_scene->clear();m_scene->setSceneRect(QRectF(QPointF(),m_frame.size()));m_scene->addPixmap(m_frame);
 const int index=m_list->currentRow();auto a=m_document.nodes();const auto n=index>=0&&index<a.size()?a[index].toObject():QJsonObject();const bool wheel=n["type"]=="KMT_STEER_WHEEL";
 for(int i=0;i<4;++i){m_keys[i]->setEnabled(!n.isEmpty()&&(wheel||i==0));m_keys[i]->setText(n.value(wheel?QStringList{"leftKey","rightKey","upKey","downKey"}[i]:"key").toString());}
 m_range->setEnabled(wheel);if(wheel)m_range->setValue(n["leftOffset"].toDouble(.1));
 auto marker=[&](const char*field,const QString&label){
  if(!n[field].isObject())return;auto p=n[field].toObject();auto*item=new Marker(label,qMax(18.,m_frame.width()/35.));m_scene->addItem(item);
  item->setPos(p["x"].toDouble()*m_frame.width(),p["y"].toDouble()*m_frame.height());const QString f=field;
  item->changed=[this,index,f](QPointF pos){auto nodes=m_document.nodes();if(index<0||index>=nodes.size())return;auto obj=nodes[index].toObject();obj[f]=KeymapDocument::pos(pos.x()/m_frame.width(),pos.y()/m_frame.height());m_document.setNode(index,obj);m_dirty=true;};
 };
 if(wheel)marker("centerPos","WASD");else if(n["type"]=="KMT_DRAG"){marker("startPos",tr("起点"));marker("endPos",tr("终点"));}else marker("pos",InputBinding::label(n["key"].toString()).remove("Key_"));
 m_view->fitInView(m_scene->sceneRect(),Qt::KeepAspectRatio);m_updating=false;
}
void KeymapEditor::updateNode(){
 if(m_updating)return;int i=m_list->currentRow();auto a=m_document.nodes();if(i<0||i>=a.size())return;auto n=a[i].toObject();
 if(n["type"]=="KMT_STEER_WHEEL"){QStringList fields{"leftKey","rightKey","upKey","downKey"};for(int k=0;k<4;++k)n[fields[k]]=m_keys[k]->text();for(const char*k:{"leftOffset","rightOffset","upOffset","downOffset"})n[k]=m_range->value();}else n["key"]=m_keys[0]->text();
 m_document.setNode(i,n);m_dirty=true;rebuild();
}
void KeymapEditor::load(){
 if(m_dirty&&QMessageBox::question(this,tr("未保存"),tr("丢弃当前编辑并导入？"),QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes)return;
 QString p=QFileDialog::getOpenFileName(this,tr("导入映射"),QCoreApplication::applicationDirPath()+"/keymap",tr("JSON (*.json)"));if(p.isEmpty())return;
 QFile f(p);if(!f.open(QIODevice::ReadOnly)||f.size()>1024*1024){m_error->setText(tr("文件不可读或超过 1 MiB。"));return;}QString e;if(!m_document.parse(f.readAll(),&e)){m_error->setText(e);return;}
 m_path=p;m_dirty=false;m_switch->setText(m_document.root["switchKey"].toString());m_error->clear();rebuild();
}
bool KeymapEditor::save(){
 QString e;if(!KeymapDocument::validate(m_document.root,&e)){m_error->setText(e);return false;}QString dir=QCoreApplication::applicationDirPath()+"/keymap";QDir().mkpath(dir);
 QString p=QFileDialog::getSaveFileName(this,tr("保存映射"),m_path.isEmpty()?dir+"/my-keymap.json":m_path,tr("JSON (*.json)"));if(p.isEmpty())return false;
 QSaveFile f(p);auto bytes=QJsonDocument(m_document.root).toJson();if(!f.open(QIODevice::WriteOnly)||f.write(bytes)!=bytes.size()||!f.commit()){m_error->setText(tr("保存失败，原文件保持不变。"));return false;}
 m_path=p;m_dirty=false;m_error->setText(tr("已保存。应用后按映射切换键启用。"));return true;
}
void KeymapEditor::apply(){if(save())accept();}
void KeymapEditor::reject(){if(m_dirty&&QMessageBox::question(this,tr("未保存"),tr("丢弃当前修改？"),QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes)return;QDialog::reject();}
