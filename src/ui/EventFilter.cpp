//
// Created by Tobias Hieta on 07/03/16.
//

#include "EventFilter.h"
#include "system/SystemComponent.h"
#include "settings/SettingsComponent.h"
#include "input/InputComponent.h"
#include "input/InputKeyboard.h"
#include "system/MenuComponent.h"
#include "KonvergoWindow.h"

#include <QGuiApplication>
#include <QInputMethodQueryEvent>
#include <QKeyEvent>
#include <QObject>

#define MIN_KEYDOWN_INTERVAL_MSEC 150

static QStringList desktopWhiteListedKeys = { "Media Play",
                                              "Media Pause",
                                              "Media Stop",
                                              "Media Next",
                                              "Media Previous",
                                              "Media Rewind",
                                              "Media FastForward",
                                              "Back"};
// These just happen to be mostly the same.

///////////////////////////////////////////////////////////////////////////////////////////////////
static QString keyEventToKeyString(QKeyEvent *kevent)
{
  // We ignore the KeypadModifier here since it's practically useless
  QKeySequence modifiers(kevent->modifiers() &= ~Qt::KeypadModifier);

  QKeySequence keySeq(kevent->key());
  QString key = keySeq.toString();

  // Qt tends to make up something weird for keys which don't cleanly map to text.
  // See e.g. QKeySequencePrivate::keyName() in the Qt sources.
  // We can't really know for sure which names are "good" or "bad", so we simply
  // allow printable latin1 characters, and mangle everything else.
  if ((key.size() > 0 && (key[0].unicode() < 32 || key[0].unicode() > 255)))
  {
    if (kevent->nativeVirtualKey() != 0)
    {
      key = "0x" + QString::number(kevent->nativeVirtualKey(), 16) + "V";
    }
    else
    {
      QString properKey;
      for (int n = 0; n < key.size(); n++)
        properKey += QString(n > 0 ? "+" : "") + "0x" + QString::number(key[n].unicode(), 16) + "Q";
      key = properKey;
    }
  }

  return modifiers.toString() + key;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
static bool textEntryFocused()
{
  QObject* focus = QGuiApplication::focusObject();
  if (!focus)
    return false;

  QInputMethodQueryEvent query(Qt::ImEnabled);
  QCoreApplication::sendEvent(focus, &query);

  return query.value(Qt::ImEnabled).toBool();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
static bool belongsToTextEntry(QKeyEvent* kevent)
{
  if (kevent->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
    return false;

  switch (kevent->key())
  {
    case Qt::Key_Escape:
      return false;
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Home:
    case Qt::Key_End:
    case Qt::Key_Backspace:
    case Qt::Key_Delete:
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
      return true;
    default:
      return !kevent->text().isEmpty() && kevent->text().at(0).isPrint();
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool EventFilter::eventFilter(QObject* watched, QEvent* event)
{
  KonvergoWindow* window = qobject_cast<KonvergoWindow*>(parent());

  if (window && window->property("webDesktopMode").toBool())
  {
    // For desktop mode we don't want fullblown keyboard handling in
    // the host yet. We just want to handle some specific keyboard
    // events.
    //
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease || event->type() == QEvent::ShortcutOverride)
    {
      QKeyEvent* key = dynamic_cast<QKeyEvent*>(event);

      if (key)
      {
        InputBase::InputkeyState keystatus;

        if (event->type() == QEvent::KeyPress)
          keystatus = InputBase::KeyDown;
        else
          keystatus = InputBase::KeyUp;

        QString seq = keyEventToKeyString(key);
        if (desktopWhiteListedKeys.contains(seq))
        {
          InputKeyboard::Get().keyPress(seq, keystatus);
          return true;
        }
      }
    }
    return QObject::eventFilter(watched, event);
  }

  SystemComponent& system = SystemComponent::Get();

  // ignore mouse events if mouse is disabled
  if  (SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, "disablemouse").toBool() &&
       ((event->type() == QEvent::MouseMove) ||
        (event->type() == QEvent::MouseButtonPress) ||
        (event->type() == QEvent::MouseButtonRelease) ||
        (event->type() == QEvent::MouseButtonDblClick) ||
        (event->type() == QEvent::Wheel)))
  {
    return true;
  }

  if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
  {
    // In konvergo we intercept all keyboard events and translate them
    // into web client actions. We need to do this so that we can remap
    // keyboard buttons to different events.
    //

    InputBase::InputkeyState keystatus;

    if (event->type() == QEvent::KeyPress)
      keystatus = InputBase::KeyDown;
    else
      keystatus = InputBase::KeyUp;

    QKeyEvent* kevent = dynamic_cast<QKeyEvent*>(event);
    if (!kevent)
      return QObject::eventFilter(watched, event);

    QString keyName = keyEventToKeyString(kevent);

    MenuComponent& menu = MenuComponent::Get();
    if (menu.keyboardGrabbed() && keyName != menu.menuKey())
    {
      m_currentKeyDown = -1;
      InputComponent::Get().cancelAutoRepeat();
      return QObject::eventFilter(watched, event);
    }

    if (belongsToTextEntry(kevent) && textEntryFocused())
    {
      m_currentKeyDown = -1;
      InputComponent::Get().cancelAutoRepeat();
      return QObject::eventFilter(watched, event);
    }

    if (keystatus == InputBase::KeyDown)
    {
      // Swallow auto-repeated keys (isAutoRepeat doesn't always work - QTBUG-57335)
      if (kevent->key() != Qt::Key_Shift && kevent->key() != Qt::Key_Control &&
          kevent->key() != Qt::Key_Alt && kevent->key() != Qt::Key_Meta &&
          kevent->key() != Qt::Key_AltGr)
      {
        if (m_currentKeyDown == kevent->key())
          return true;
        m_currentKeyDown = kevent->key();

        if (m_lastKeyDown == kevent->key() && m_lastKeyDownTime.isValid() &&
            m_lastKeyDownTime.elapsed() < MIN_KEYDOWN_INTERVAL_MSEC)
          return true;

        m_lastKeyDown = kevent->key();
        m_lastKeyDownTime.start();
      }
    }
    else
      m_currentKeyDown = -1;

    system.setCursorVisibility(false);
    if (kevent->spontaneous() && !kevent->isAutoRepeat())
    {
      InputKeyboard::Get().keyPress(keyName, keystatus);
      return true;
    }
  }
  else if (event->type() == QEvent::MouseMove)
  {
    system.setCursorVisibility(true);
  }
  else if (event->type() == QEvent::Wheel)
  {
    system.setCursorVisibility(true);
  }
  else if (event->type() == QEvent::MouseButtonPress)
  {
    // ignore right clicks that would show context menu
    QMouseEvent *mouseEvent = dynamic_cast<QMouseEvent*>(event);
    if ((mouseEvent) && (mouseEvent->button() == Qt::RightButton))
      return true;
  }
  else if (event->type() == QEvent::Drop)
  {
    // QtWebEngine would accept the drop and unload web-client.
    return true;
  }

  return QObject::eventFilter(watched, event);
}
