#ifndef INPUTLIRC_H
#define INPUTLIRC_H

#include <QLocalSocket>
#include <QSocketNotifier>
#include "input/InputComponent.h"

class InputLIRC : public InputBase
{
  Q_OBJECT
private:
  QLocalSocket* socket;
  QSocketNotifier* socketNotifier;

  bool connectToLIRC();
  void disconnectFromLIRC();
  bool isConnected();

public:
  InputLIRC(QObject* parent);
  ~InputLIRC();

  const char* inputName() override { return "LIRC"; }
  bool initInput() override;

private Q_SLOTS:
  void connected();
  void disconnected();
  void socketerror(QLocalSocket::LocalSocketError socketError);
  void read(QSocketDescriptor handle, QSocketNotifier::Type type);
};

#endif // INPUTLIRC_H
