#ifndef KEYMAPDOCUMENT_H
#define KEYMAPDOCUMENT_H
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaEnum>
#include <QSet>
#include <QPointF>
#include <cmath>
#include "inputbinding.h"
class KeymapDocument {
public:
 QJsonObject root{{"switchKey","Key_QuoteLeft"},{"keyMapNodes",QJsonArray()},{"mouseLookEnabled",false}};
 static bool binding(const QString&s){return !InputBinding::identity(s).isEmpty();}
 static QJsonObject pos(double x,double y){return {{"x",x},{"y",y}};}
 static bool point(const QJsonObject&n,const char*field){const auto p=n.value(field);if(!p.isObject())return false;for(const char*k:{"x","y"}){auto v=p.toObject().value(k);double d=v.toDouble(-1);if(!v.isDouble()||!std::isfinite(d)||d<0||d>=1)return false;}return true;}
 static bool number(const QJsonObject&n,const char*k,double lo,double hi){auto v=n.value(k);double d=v.toDouble(-1);return v.isDouble()&&std::isfinite(d)&&d>=lo&&d<=hi;}
 static bool safeValue(const QJsonValue &v,int depth=0){
  if(depth>24)return false;
  if(v.isString())return v.toString().size()<=4096;
  if(v.isDouble())return std::isfinite(v.toDouble());
  if(v.isArray()){const auto a=v.toArray();if(a.size()>1000)return false;for(const auto &x:a)if(!safeValue(x,depth+1))return false;}
  if(v.isObject()){const auto o=v.toObject();if(o.size()>128)return false;for(auto i=o.begin();i!=o.end();++i){if(i.key()=="__proto__"||i.key()=="prototype"||i.key()=="constructor"||i.key().size()>128||!safeValue(i.value(),depth+1))return false;}}
  return true;
 }
 static bool integer(const QJsonObject&n,const char*k,double lo,double hi){return number(n,k,lo,hi)&&std::floor(n[k].toDouble())==n[k].toDouble();}
 static bool validate(const QJsonObject&o,QString*error=nullptr){
  auto fail=[&](const QString&s){if(error)*error=s;return false;};
  if(!safeValue(o))return fail(QStringLiteral("JSON 字段过深、过大或含保留字段。"));
  if(!o.value("switchKey").isString()||!binding(o.value("switchKey").toString()))return fail(QStringLiteral("切换按键无效。"));
  if(!o.value("keyMapNodes").isArray()||o["keyMapNodes"].toArray().size()>500)return fail(QStringLiteral("映射数量无效（上限 500）。"));
  if(o.contains("mouseLookEnabled")&&!o["mouseLookEnabled"].isBool())return fail(QStringLiteral("鼠标视角开关必须是布尔值。"));
  if(o["mouseLookEnabled"].toBool(false)&&!o["mouseMoveMap"].isObject())return fail(QStringLiteral("启用鼠标视角需要已有的视角配置。"));
  QSet<QString>keys{InputBinding::identity(o["switchKey"].toString())};
  auto bind=[&](const QJsonObject&n,const char*k){auto v=n.value(k);QString s=InputBinding::identity(v.toString());if(!v.isString()||s.isEmpty()||keys.contains(s))return false;keys.insert(s);return true;};
  for(const auto&v:o["keyMapNodes"].toArray()){
   if(!v.isObject())return fail(QStringLiteral("映射必须为对象。"));auto n=v.toObject();const QString type=n["type"].toString();
   if(n.contains("opacity")&&!number(n,"opacity",0,1))return fail(QStringLiteral("透明度参数无效。"));
   if(n.contains("androidKey")&&!integer(n,"androidKey",0,1000))return fail(QStringLiteral("Android 按键编号无效。"));
   if(type=="KMT_STEER_WHEEL"){
    if(!point(n,"centerPos"))return fail(QStringLiteral("摇杆中心无效。"));
    for(const char*k:{"leftKey","rightKey","upKey","downKey"})if(!bind(n,k))return fail(QStringLiteral("摇杆按键无效或冲突。"));
    for(const char*k:{"leftOffset","rightOffset","upOffset","downOffset"})if(!number(n,k,0.001,0.5))return fail(QStringLiteral("摇杆范围无效。"));
    auto p=n["centerPos"].toObject();const double x=p["x"].toDouble(),y=p["y"].toDouble();if(x-n["leftOffset"].toDouble()<0||x+n["rightOffset"].toDouble()>=1||y-n["upOffset"].toDouble()<0||y+n["downOffset"].toDouble()>=1)return fail(QStringLiteral("摇杆范围超出画面。"));
   }else{
    if(!bind(n,"key"))return fail(QStringLiteral("绑定按键无效或与其他控件/切换键冲突。"));
    if(type=="KMT_CLICK"||type=="KMT_CLICK_TWICE"){
     if(!point(n,"pos"))return fail(QStringLiteral("点击位置无效。"));if(type=="KMT_CLICK"&&!n["switchMap"].isBool())return fail(QStringLiteral("switchMap 必须为布尔值。"));
    }else if(type=="KMT_DRAG"){
     if(!point(n,"startPos")||!point(n,"endPos"))return fail(QStringLiteral("滑动坐标无效。"));if(n.contains("dragSpeed")&&!number(n,"dragSpeed",0.01,1.0))return fail(QStringLiteral("滑动速度无效。"));if(n.contains("startDelay")&&!integer(n,"startDelay",0,600000))return fail(QStringLiteral("起始等待无效。"));
    }else if(type=="KMT_CLICK_MULTI"){
     auto a=n["clickNodes"].toArray();if(a.isEmpty()||a.size()>50)return fail(QStringLiteral("多次点击序列无效。"));for(const auto&c:a){auto obj=c.toObject();if(!point(obj,"pos")||!integer(obj,"delay",0,600000)||(obj.contains("order")&&!integer(obj,"order",0,1000)))return fail(QStringLiteral("多次点击参数无效。"));}
    }else if(type=="KMT_ANDROID_KEY"){if(!integer(n,"androidKey",1,1000))return fail(QStringLiteral("Android 按键编号无效。"));}else return fail(QStringLiteral("暂不支持这种映射类型。"));
   }
  }
  if(o.contains("mouseMoveMap")){
   if(!o["mouseMoveMap"].isObject())return fail(QStringLiteral("视角映射无效。"));const auto m=o["mouseMoveMap"].toObject();if(!point(m,"startPos"))return fail(QStringLiteral("视角起点无效。"));
   if(m.contains("speedRatio")){if(!number(m,"speedRatio",0.00225,10000))return fail(QStringLiteral("视角灵敏度无效。"));}else if(!number(m,"speedRatioX",0.001,10000)||!number(m,"speedRatioY",0.001,10000))return fail(QStringLiteral("视角灵敏度无效。"));
   if(m.contains("smallEyes")){
    if(!m["smallEyes"].isObject())return fail(QStringLiteral("小眼睛配置无效。"));
    const auto e=m["smallEyes"].toObject();
    if(e["type"]!="KMT_CLICK"||!e["switchMap"].isBool()||!point(e,"pos")||!e["key"].isString()||!binding(e["key"].toString()))return fail(QStringLiteral("小眼睛参数无效。"));
    if(e.contains("enabled")&&!e["enabled"].isBool())return fail(QStringLiteral("小眼睛开关无效。"));
    if(o["mouseLookEnabled"].toBool(false)&&e["enabled"].toBool(true)&&!bind(e,"key"))return fail(QStringLiteral("小眼睛按键与其他映射冲突。"));
   }
  }
  return true;
 }
 bool parse(const QByteArray&bytes,QString*error=nullptr){if(bytes.size()>1024*1024){if(error)*error=QStringLiteral("映射文件超过 1 MiB。");return false;}QJsonParseError e;auto d=QJsonDocument::fromJson(bytes,&e);if(e.error!=QJsonParseError::NoError||!d.isObject()){if(error)*error=QStringLiteral("不是有效 JSON 对象。");return false;}auto candidate=d.object();if(!candidate.contains("mouseLookEnabled"))candidate["mouseLookEnabled"]=candidate.value("mouseMoveMap").isObject();if(!validate(candidate,error))return false;root=candidate;return true;}
 QJsonArray nodes()const{return root["keyMapNodes"].toArray();}
 void setNode(int index,const QJsonObject&n){auto a=nodes();if(index>=0&&index<a.size()){a[index]=n;root["keyMapNodes"]=a;}}
 int add(const QString&type){auto a=nodes();QJsonObject n{{"type",type}};
  if(type=="KMT_STEER_WHEEL")n={{"type",type},{"centerPos",pos(.25,.7)},{"leftKey","Key_A"},{"rightKey","Key_D"},{"upKey","Key_W"},{"downKey","Key_S"},{"leftOffset",.1},{"rightOffset",.1},{"upOffset",.1},{"downOffset",.1}};
  else if(type=="KMT_DRAG")n={{"type",type},{"key","Key_F"},{"startPos",pos(.4,.6)},{"endPos",pos(.7,.4)},{"dragSpeed",1.0},{"startDelay",0}};
  else n={{"type",type},{"key","Key_Space"},{"pos",pos(.6,.6)},{"switchMap",false}};
  a.append(n);root["keyMapNodes"]=a;return a.size()-1;
 }
};
#endif
