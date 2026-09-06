#ifndef ACTIONMACROHOTKEY_H
#define ACTIONMACROHOTKEY_H
#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QKeyEvent>
#include <QPointer>
#include <QVector>
#include "../QtScrcpyCore/include/QtScrcpyCore.h"
#ifdef Q_OS_WIN
#include <windows.h>
#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#endif
#endif
// One handler owns both hotkeys. Pause never auto-resumes interrupted gestures.
class ActionMacroHotkey : public QObject, public QAbstractNativeEventFilter {
public:
 static ActionMacroHotkey *instance(){static QPointer<ActionMacroHotkey> current;if(!current)current=new ActionMacroHotkey(qApp);return current.data();}
 void watch(qsc::IDevice*d){if(!d)return;for(const auto&i:m_devices)if(i==d)return;m_devices.append(QPointer<qsc::IDevice>(d));}
 bool globalAvailable()const{return m_globalAvailable;}
 bool pauseGlobalAvailable()const{return m_pauseGlobalAvailable;}
 void pauseAll(){const auto devices=m_devices;for(const auto&d:devices)if(d)d->pauseActionMacro();}
 void stopAll(){const auto devices=m_devices;for(const auto&d:devices){if(d)d->stopActionPlayback();if(d)d->stopActionRecording();if(d)d->releaseKeyboard();}for(int i=m_devices.size()-1;i>=0;--i)if(!m_devices.at(i))m_devices.remove(i);}
 ~ActionMacroHotkey()override{
#ifdef Q_OS_WIN
  if(m_globalAvailable)UnregisterHotKey(nullptr,kHotkeyId);
  if(m_pauseGlobalAvailable)UnregisterHotKey(nullptr,kPauseHotkeyId);
#endif
  if(qApp){qApp->removeNativeEventFilter(this);qApp->removeEventFilter(this);}
 }
protected:
 bool eventFilter(QObject*watched,QEvent*event)override{
  Q_UNUSED(watched);
  if(event->type()!=QEvent::ShortcutOverride&&event->type()!=QEvent::KeyPress&&event->type()!=QEvent::KeyRelease)return false;
  const auto*key=static_cast<QKeyEvent*>(event);const auto mods=key->modifiers()&(Qt::ControlModifier|Qt::ShiftModifier|Qt::AltModifier|Qt::MetaModifier);
  if((key->key()!=Qt::Key_X&&key->key()!=Qt::Key_P)||mods!=(Qt::ControlModifier|Qt::ShiftModifier))return false;
  if(event->type()==QEvent::KeyPress&&!key->isAutoRepeat()){if(key->key()==Qt::Key_P)pauseAll();else stopAll();}
  event->accept();return true;
 }
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
 bool nativeEventFilter(const QByteArray&type,void*message,qintptr*result)override
#else
 bool nativeEventFilter(const QByteArray&type,void*message,long*result)override
#endif
 {
  Q_UNUSED(type);Q_UNUSED(result);
#ifdef Q_OS_WIN
  const MSG*msg=static_cast<const MSG*>(message);
  if(msg&&msg->message==WM_HOTKEY&&msg->wParam==kHotkeyId){stopAll();return true;}
  if(msg&&msg->message==WM_HOTKEY&&msg->wParam==kPauseHotkeyId){pauseAll();return true;}
#else
  Q_UNUSED(message);
#endif
  return false;
 }
private:
 explicit ActionMacroHotkey(QObject*parent):QObject(parent){
  qApp->installEventFilter(this);qApp->installNativeEventFilter(this);
#ifdef Q_OS_WIN
  m_globalAvailable=RegisterHotKey(nullptr,kHotkeyId,MOD_CONTROL|MOD_SHIFT|MOD_NOREPEAT,'X')!=0;
  m_pauseGlobalAvailable=RegisterHotKey(nullptr,kPauseHotkeyId,MOD_CONTROL|MOD_SHIFT|MOD_NOREPEAT,'P')!=0;
#endif
 }
 enum{kHotkeyId=0x514D,kPauseHotkeyId=0x514E};
 bool m_globalAvailable=false,m_pauseGlobalAvailable=false;
 QVector<QPointer<qsc::IDevice>>m_devices;
};
#endif
