#ifndef HTTP_LISTENER_H
#define HTTP_LISTENER_H

#include <QHostAddress>
#include <QString>

#include <QtHttpServer/QAbstractHttpServer>
#include <QtHttpServer/QHttpServerRequest>

#include "HttpResponse.h"

QString httpMethodName(QHttpServerRequest::Method method);

class HttpListener : public QAbstractHttpServer
{
  Q_OBJECT

public:
  explicit HttpListener(QObject* parent = nullptr);

  bool listen(const QHostAddress& address, quint16 port);

signals:
  void newRequest(const QHttpServerRequest& request, HttpResponse* response);

protected:
  bool handleRequest(const QHttpServerRequest& request, QHttpServerResponder& responder) override;
  void missingHandler(const QHttpServerRequest& request, QHttpServerResponder& responder) override;
};

#endif
