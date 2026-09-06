#ifndef MACROEXECUTIONOPTIONS_H
#define MACROEXECUTIONOPTIONS_H
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSpinBox>
#include <cmath>
// Optional UI metadata; macro event formats v1/v2 remain compatible.
class MacroExecutionOptions : public QWidget {
public:
 explicit MacroExecutionOptions(QWidget *parent=nullptr):QWidget(parent){
  auto*layout=new QFormLayout(this);layout->setContentsMargins(0,0,0,0);
  mode=new QComboBox(this);mode->setObjectName("macroStopMode");mode->addItems({tr("按执行次数"),tr("按执行时间"),tr("无限循环至手动停止")});
  count=new QSpinBox(this);count->setObjectName("macroRepeatCount");count->setRange(1,9999);count->setValue(1);
  seconds=new QSpinBox(this);seconds->setObjectName("macroLimitSeconds");seconds->setRange(1,86400);seconds->setValue(600);seconds->setSuffix(tr(" 秒"));
  interval=new QDoubleSpinBox(this);interval->setObjectName("macroIntervalSeconds");interval->setRange(0,600);interval->setDecimals(3);interval->setValue(0.5);interval->setSuffix(tr(" 秒"));
  speed=new QComboBox(this);speed->setObjectName("macroSpeed");for(double v:{0.25,0.5,1.,1.5,2.,3.,4.,6.,8.})speed->addItem(QString::number(v)+QStringLiteral("×"),v);speed->setCurrentIndex(2);
  layout->addRow(tr("停止条件"),mode);layout->addRow(tr("执行次数"),count);layout->addRow(tr("执行时间"),seconds);layout->addRow(tr("循环间隔"),interval);layout->addRow(tr("执行倍率（最高 8×）"),speed);
  connect(mode,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int){refresh();});refresh();
 }
 int repeats()const{return mode->currentIndex()==0?count->value():0;}
 int intervalMs()const{return qRound(interval->value()*1000);}
 qint64 limitMs()const{return mode->currentIndex()==1?qint64(seconds->value())*1000:0;}
 double multiplier()const{return speed->currentData().toDouble();}
 void refresh(){count->setEnabled(mode->currentIndex()==0);seconds->setEnabled(mode->currentIndex()==1);}
 QJsonObject json()const{return {{"version",1},{"stopMode",mode->currentIndex()},{"repeatCount",count->value()},{"durationSeconds",seconds->value()},{"intervalMs",intervalMs()},{"speed",multiplier()}};}
 static bool integer(const QJsonObject&o,const char*k,int lo,int hi){const auto v=o.value(k);const double n=v.toDouble(-1);return v.isDouble()&&std::isfinite(n)&&std::floor(n)==n&&n>=lo&&n<=hi;}
 bool restore(const QJsonObject&o){
  mode->setCurrentIndex(0);count->setValue(1);seconds->setValue(600);interval->setValue(0.5);speed->setCurrentIndex(2);
  if(o.isEmpty())return true;
  if(!integer(o,"version",1,1)||!integer(o,"stopMode",0,2)||!integer(o,"repeatCount",1,9999)||!integer(o,"durationSeconds",1,86400)||!integer(o,"intervalMs",0,600000)||!o.value("speed").isDouble())return false;
  const double s=o.value("speed").toDouble();const int idx=speed->findData(s);if(idx<0||!std::isfinite(s)||s<0.25||s>8)return false;
  mode->setCurrentIndex(o["stopMode"].toInt());count->setValue(o["repeatCount"].toInt());seconds->setValue(o["durationSeconds"].toInt());interval->setValue(o["intervalMs"].toInt()/1000.0);speed->setCurrentIndex(idx);refresh();return true;
 }
 static bool saveTo(const QString&path,const QJsonObject&options){
  QFile f(path);if(!f.open(QIODevice::ReadOnly)||f.size()>64LL*1024*1024)return false;
  QJsonParseError error;QJsonDocument d=QJsonDocument::fromJson(f.readAll(),&error);f.close();if(error.error!=QJsonParseError::NoError||!d.isObject())return false;
  auto o=d.object();o["executionSettings"]=options;const auto data=QJsonDocument(o).toJson(QJsonDocument::Compact);if(data.size()>64LL*1024*1024)return false;
  QSaveFile out(path);return out.open(QIODevice::WriteOnly)&&out.write(data)==data.size()&&out.commit();
 }
 bool loadFrom(const QString&path){QFile f(path);if(!f.open(QIODevice::ReadOnly)||f.size()>64LL*1024*1024)return false;const auto o=QJsonDocument::fromJson(f.readAll()).object();if(o.contains("executionSettings")&&!o.value("executionSettings").isObject()){restore({});return false;}return restore(o.value("executionSettings").toObject());}
 QComboBox *mode,*speed;QSpinBox *count,*seconds;QDoubleSpinBox *interval;
};
#endif
