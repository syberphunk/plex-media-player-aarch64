//
// Created by Tobias Hieta on 28/08/15.
//

#ifndef KONVERGO_HELPERLAUNCHER_H
#define KONVERGO_HELPERLAUNCHER_H

#include <QLocalSocket>
#include <QObject>
#include <QProcess>
#include <QJsonObject>

#include "LocalJsonClient.h"
#include "tools/helper/HelperSocket.h"
#include "Version.h"
#include "utils/Utils.h"


class HelperLauncher : public QObject
{
  Q_OBJECT
  DEFINE_SINGLETON(HelperLauncher)
public:
  bool connectToHelper();
  static QString HelperPath();
  void stop();
  void start();

private Q_SLOTS:
  void gotMessage(const QVariantMap& message);
  void socketError(QLocalSocket::LocalSocketError error);
  void didConnect();
  void launch();
  void socketDisconnect();
  bool killHelper();

private:
  explicit HelperLauncher(QObject* parent = nullptr);

  QProcess* m_helperProcess;
  LocalJsonClient* m_jsonClient;

  void updateClientId();
  bool helperEnabled();

};

#endif //KONVERGO_HELPERLAUNCHER_H
