#ifndef HTTP_LISTENER_H
#define HTTP_LISTENER_H

#include <QHostAddress>
#include <QList>
#include <QString>
#include <QStringList>

#include <QtHttpServer/QAbstractHttpServer>
#include <QtHttpServer/QHttpServerRequest>

#include "HttpResponse.h"

class QWebSocket;

QString httpMethodName(QHttpServerRequest::Method method);

class HttpListener : public QAbstractHttpServer
{
  Q_OBJECT

public:
  explicit HttpListener(QObject* parent = nullptr);

  bool listen(const QHostAddress& address, quint16 port);

  void acceptWebSocketPath(const QString& path);
  void broadcast(const QByteArray& payload);

signals:
  void newRequest(const QHttpServerRequest& request, HttpResponse* response);

protected:
  bool handleRequest(const QHttpServerRequest& request, QHttpServerResponder& responder) override;
  void missingHandler(const QHttpServerRequest& request, QHttpServerResponder& responder) override;

private slots:
  void adoptWebSocket();

private:
  QStringList m_webSocketPaths;
  QList<QWebSocket*> m_webSockets;
};

#endif
