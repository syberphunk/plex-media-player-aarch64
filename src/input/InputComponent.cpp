#include "QsLog.h"
#include "InputComponent.h"
#include "settings/SettingsComponent.h"
#include "system/SystemComponent.h"
#include "system/MenuComponent.h"
#include "power/PowerComponent.h"
#include "player/PlayerComponent.h"
#include "core/Globals.h"
#include "ui/KonvergoWindow.h"
#include "InputKeyboard.h"
#include "InputSocket.h"
#include "InputRoku.h"

#include <QCoreApplication>
#include <QKeyEvent>

#ifdef HAVE_SDL
#include "InputSDL.h"
#endif

#ifdef HAVE_LIRC
#include "InputLIRC.h"
#endif

#ifdef HAVE_CEC
#include "InputCEC.h"
#endif

#define LONG_HOLD_MSEC 500
#define INITAL_AUTOREPEAT_MSEC 450
#define AUTOREPEAT_MSEC 180
#define AUTOREPEAT_MAX_MSEC 4000
#define ACTION_RATE_MSEC 120
#define ACTION_DEBOUNCE_MSEC 400
#define SOURCE_OVERLAP_MSEC 150

///////////////////////////////////////////////////////////////////////////////////////////////////
static bool actionRepeats(const QString& action)
{
  static const QStringList repeatable = {
    "up",
    "down",
    "left",
    "right",
    "seek_forward",
    "seek_backward",
    "increase_volume",
    "decrease_volume",
    "increase_audio_delay",
    "decrease_audio_delay",
    "increase_subtitles_delay",
    "decrease_subtitles_delay",
  };

  return repeatable.contains(action);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
static int overlayKey(const QString& action)
{
  static const QHash<QString, int> keys = {
    { "up", Qt::Key_Up },
    { "down", Qt::Key_Down },
    { "left", Qt::Key_Left },
    { "right", Qt::Key_Right },
    { "enter", Qt::Key_Return },
    { "back", Qt::Key_Escape },
  };

  return keys.value(action, 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
InputComponent::InputComponent(QObject* parent) : ComponentBase(parent), m_lastSourceSeen(0)
{
  m_mappings = new InputMapping(this);

  m_actionClock.start();

  m_autoRepeatTimer = new QTimer(this);
  connect(m_autoRepeatTimer, &QTimer::timeout, this, [this]()
  {
    if (m_autoRepeatActions.isEmpty() ||
        (m_autoRepeatStarted.isValid() && m_autoRepeatStarted.elapsed() > AUTOREPEAT_MAX_MSEC))
    {
      cancelAutoRepeat();
      return;
    }

    dispatchActions(m_autoRepeatActions);

    m_autoRepeatTimer->setInterval(AUTOREPEAT_MSEC);
  });
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool InputComponent::addInput(InputBase* base)
{
  if (!base->initInput())
  {
    QLOG_WARN() << "Failed to init input:" << base->inputName();
    return false;
  }

  QLOG_INFO() << "Successfully inited input:" << base->inputName();
  m_inputs.push_back(base);

  // we connect to the provider receivedInput signal, then we check if the name
  // needs to be remaped in remapInput and then finally send it out to JS land.
  //
  connect(base, &InputBase::receivedInput, this, &InputComponent::remapInput);

  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool InputComponent::componentInitialize()
{
  // load our input mappings
  m_mappings->loadMappings();

  addInput(&InputKeyboard::Get());
  addInput(new InputSocket(this));
  addInput(new InputRoku(this));

#ifdef HAVE_SDL
  if (SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, "sdlEnabled").toBool())
    addInput(new InputSDL(this));
#endif
#ifdef HAVE_LIRC
  addInput(new InputLIRC(this));
#endif
#ifdef HAVE_CEC
  if (SettingsComponent::Get().value(SETTINGS_SECTION_CEC, "enable").toBool())
    addInput(new InputCEC(this));
#endif

  return true;
}

/////////////////////////////////////////////////////////////////////////////////////////
void InputComponent::handleAction(const QString& action)
{
  if (action.startsWith("host:"))
  {
    QStringList argList = action.mid(5).split(" ");
    QString hostCommand = argList.value(0);
    QString hostArguments;

    if (argList.size() > 1)
    {
      argList.pop_front();
      hostArguments = argList.join(" ");
    }

    QLOG_DEBUG() << "Got host command:" << hostCommand << "arguments:" << hostArguments;
    if (m_hostCommands.contains(hostCommand))
    {
      ReceiverSlot* recvSlot = m_hostCommands.value(hostCommand);
      if (recvSlot)
      {
        if (recvSlot->m_function)
        {
          QLOG_DEBUG() << "Invoking anonymous function";
          recvSlot->m_function();
        }
        else
        {
          QLOG_DEBUG() << "Invoking slot" << qPrintable(recvSlot->m_slot.data());
          bool invoked;

          if (recvSlot->m_hasArguments)
            invoked = QMetaObject::invokeMethod(recvSlot->m_receiver, recvSlot->m_slot.data(),
                                               Qt::AutoConnection, Q_ARG(const QString&, hostArguments));
          else
            invoked = QMetaObject::invokeMethod(recvSlot->m_receiver, recvSlot->m_slot.data(),
                                               Qt::AutoConnection);

          if (!invoked)
          {
            QLOG_ERROR() << "Invoking slot" << qPrintable(recvSlot->m_slot.data()) << "failed!";
          }
        }
      }
    }
    else
    {
      QLOG_WARN() << "No such host command:" << hostCommand;
    }
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
QStringList InputComponent::throttleActions(const QStringList& actions, bool inPlayback)
{
  QStringList passed;
  const qint64 now = m_actionClock.elapsed();

  for (const auto& action : actions)
  {
    const bool rateLimited = actionRepeats(action) && !inPlayback;
    const qint64 window = rateLimited ? ACTION_RATE_MSEC : ACTION_DEBOUNCE_MSEC;
    const bool expired = !m_actionSeen.contains(action) || now - m_actionSeen.value(action) >= window;

    if (expired || !rateLimited)
      m_actionSeen.insert(action, now);

    if (expired)
      passed.append(action);
    else
      QLOG_DEBUG() << "Dropped repeated input action:" << action;
  }

  return passed;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void InputComponent::remapInput(const QString &source, const QString &keycode, InputBase::InputkeyState keyState)
{
  QLOG_DEBUG() << "Input received: source:" << source << "keycode:" << keycode << ":" << keyState;

  emit receivedInput();

  if (keyState == InputBase::KeyUp)
  {
    if (source == m_suppressedSource)
    {
      m_suppressedSource.clear();
      return;
    }

    cancelAutoRepeat();

    if (!m_currentLongPressAction.isEmpty())
    {
      QString type;
      if (m_longHoldTimer.elapsed() > LONG_HOLD_MSEC)
        type = "long";
      else
        type = "short";

      QString action = m_currentLongPressAction.value(type).toString();

      m_currentLongPressAction.clear();

      QLOG_DEBUG() << "Input action (" + type + "):" << action;
      dispatchActions(QStringList{action});
    }

    return;
  }

  const qint64 arrived = m_actionClock.elapsed();

  if (!m_lastSource.isEmpty() && source != m_lastSource &&
      arrived - m_lastSourceSeen < SOURCE_OVERLAP_MSEC)
  {
    QLOG_DEBUG() << "Dropped overlapping input from source:" << source;
    m_suppressedSource = source;
    return;
  }

  if (source == m_suppressedSource)
    m_suppressedSource.clear();

  m_lastSource = source;
  m_lastSourceSeen = arrived;

  QStringList queuedActions;
  m_autoRepeatActions.clear();

  auto actions = m_mappings->mapToAction(source, keycode);
  for (auto action : actions)
  {
    if (action.typeId() == QMetaType::QString)
    {
      queuedActions.append(action.toString());
      if (actionRepeats(action.toString()))
        m_autoRepeatActions.append(action.toString());
    }
    else if (action.typeId() == QMetaType::QVariantMap)
    {
      QVariantMap map = action.toMap();
      if (map.contains("long"))
      {
        // Don't overwrite long actions if there was no key up event yet.
        // (It could be a key autorepeated by Qt.)
        if (m_currentLongPressAction.isEmpty())
        {
          m_longHoldTimer.start();
          m_currentLongPressAction = map;
        }
      }
      else if (map.contains("short"))
      {
        queuedActions.append(map.value("short").toString());
      }
    }
    else if (action.typeId() == QMetaType::QVariantList)
    {
      queuedActions.append(action.toStringList());
    }
  }

  const bool inPlayback = PlayerComponent::Get().inPlayback();

  if (inPlayback)
    cancelAutoRepeat();

  if (!m_autoRepeatActions.isEmpty() && keyState != InputBase::KeyPressed)
  {
    m_autoRepeatStarted.start();
    m_autoRepeatTimer->start(INITAL_AUTOREPEAT_MSEC);
  }

  dispatchActions(throttleActions(queuedActions, inPlayback));
}

/////////////////////////////////////////////////////////////////////////////////////////
void InputComponent::dispatchActions(const QStringList& actions)
{
  QStringList webActions;

  for (const auto& action : actions)
  {
    if (action.startsWith("host:"))
      handleAction(action);
    else
      webActions.append(action);
  }

  if (webActions.isEmpty())
    return;

  if (MenuComponent::Get().keyboardGrabbed())
  {
    dispatchToOverlay(webActions);
    return;
  }

  if (SystemComponent::Get().isWebClientConnected())
  {
    QLOG_DEBUG() << "Emit input action:" << webActions;
    emit hostInput(webActions);
  }
  else
  {
    QLOG_DEBUG() << "Web Client has not connected, handling input in host instead.";
    executeActions(webActions);
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
void InputComponent::dispatchToOverlay(const QStringList& actions)
{
  KonvergoWindow* window = Globals::MainWindow();

  for (const auto& action : actions)
  {
    const int key = overlayKey(action);

    if (!window || !key)
    {
      QLOG_DEBUG() << "Overlay holds the input, dropped action:" << action;
      continue;
    }

    QLOG_DEBUG() << "Overlay holds the input, action as key:" << action;

    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);

    QCoreApplication::sendEvent(window, &press);
    QCoreApplication::sendEvent(window, &release);
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
void InputComponent::executeActions(const QStringList& actions)
{
  for (auto action : actions)
    handleAction(action);
}

/////////////////////////////////////////////////////////////////////////////////////////
void InputComponent::registerHostCommand(const QString& command, QObject* receiver, const char* slot)
{
  auto  recvSlot = new ReceiverSlot;
  recvSlot->m_receiver = receiver;
  recvSlot->m_slot = QMetaObject::normalizedSignature(slot);
  recvSlot->m_hasArguments = false;

  QLOG_DEBUG() << "Adding host command:" << qPrintable(command) << "mapped to"
               << qPrintable(QString(receiver->metaObject()->className()) + "::" + recvSlot->m_slot);

  m_hostCommands.insert(command, recvSlot);

  auto slotWithArgs = QString("%1(QString)").arg(QString::fromLatin1(recvSlot->m_slot)).toLatin1();
  auto slotWithoutArgs = QString("%1()").arg(QString::fromLatin1(recvSlot->m_slot)).toLatin1();
  if (recvSlot->m_receiver->metaObject()->indexOfMethod(slotWithArgs.data()) != -1)
  {
    QLOG_DEBUG() << "Host command maps to method with an argument.";
    recvSlot->m_hasArguments = true;
  }
  else if (recvSlot->m_receiver->metaObject()->indexOfMethod(slotWithoutArgs.data()) != -1)
  {
    QLOG_DEBUG() << "Host command maps to method without arguments.";
  }
  else
  {
    QLOG_ERROR() << "Slot for host command missing, or has incorrect signature!";
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
void InputComponent::registerHostCommand(const QString& command, std::function<void(void)> function)
{
  auto recvSlot = new ReceiverSlot;
  recvSlot->m_function = function;
  QLOG_DEBUG() << "Adding host command:" << qPrintable(command) << "mapped to anonymous function";
  m_hostCommands.insert(command, recvSlot);
}

/////////////////////////////////////////////////////////////////////////////////////////
void InputComponent::cancelAutoRepeat()
{
  m_autoRepeatTimer->stop();
  m_autoRepeatActions.clear();
}
