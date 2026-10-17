//
// Created by Tobias Hieta on 30/08/15.
//

#include "LocalJsonServer.h"
#include "Paths.h"
#include "QsLog.h"

#define MAX_LISTEN_ATTEMPTS 5
#define MAX_MESSAGE_SIZE (64 * 1024)

/////////////////////////////////////////////////////////////////////////////////////////
LocalJsonServer::LocalJsonServer(const QString& serverName, QObject* parent) : QObject(parent)
{
  m_server = new QLocalServer(this);
  m_serverName = Paths::socketName(serverName);

  connect(m_server, &QLocalServer::newConnection, this, &LocalJsonServer::serverClientConnected);
}

/////////////////////////////////////////////////////////////////////////////////////////
bool LocalJsonServer::listen()
{
  for (int attempt = 0; attempt < MAX_LISTEN_ATTEMPTS; attempt++)
  {
    if (m_server->listen(m_serverName))
    {
      QLOG_INFO() << "Listening to socket:" << m_serverName;
      return true;
    }

    if (m_server->serverError() != QAbstractSocket::AddressInUseError)
      break;

    if (!QFile(m_serverName).remove())
    {
      QLOG_WARN() << "Can't remove the socket in the way:" << m_serverName;
      return false;
    }
  }

  QLOG_WARN() << "Failed to listen to local socket:" << m_server->errorString();
  return false;
}

/////////////////////////////////////////////////////////////////////////////////////////
void LocalJsonServer::serverClientConnected()
{
  QLocalSocket* socket = m_server->nextPendingConnection();
  if (socket)
  {
    socket->setReadBufferSize(MAX_MESSAGE_SIZE);
    m_clientSockets << socket;
    connect(socket, &QLocalSocket::readyRead, this, &LocalJsonServer::clientReadyRead);
    emit clientConnected(socket);
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
bool LocalJsonServer::sendMessage(const QVariantMap& message, QLocalSocket* socket)
{
  QJsonObject obj = QJsonObject::fromVariantMap(message);

  if (obj.isEmpty())
    return false;

  QJsonDocument doc(obj);
  QByteArray data = doc.toJson(QJsonDocument::Compact);
  data.append("\r\n");

  if (!data.isEmpty())
    return (socket->write(data) == data.size());

  return false;
}

/////////////////////////////////////////////////////////////////////////////////////////
void LocalJsonServer::clientReadyRead()
{
  QLocalSocket* socket = dynamic_cast<QLocalSocket*>(sender());
  if (!socket)
    return;

  QVariantList messages = readFromSocket(socket);
  for(const QVariant& msg : messages)
    emit messageReceived(msg.toMap());
}

/////////////////////////////////////////////////////////////////////////////////////////
QVariantList LocalJsonServer::readFromSocket(QLocalSocket* socket)
{
  QVariantList lst;

  while (socket->canReadLine())
  {
    QByteArray data = socket->readLine(MAX_MESSAGE_SIZE);
    if (!data.isNull())
    {
      QJsonParseError err;
      QJsonDocument doc = QJsonDocument::fromJson(data, &err);
      if (doc.isNull())
      {
        QLOG_WARN() << "Failed to parse message from client:" << err.errorString();
        continue;
      }

      lst << doc.toVariant();
    }
  }

  return lst;
}
