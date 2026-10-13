#include "HttpListener.h"

#include <QTcpServer>

/////////////////////////////////////////////////////////////////////////////////////////
QString httpMethodName(QHttpServerRequest::Method method)
{
  switch (method)
  {
    case QHttpServerRequest::Method::Get: return "GET";
    case QHttpServerRequest::Method::Put: return "PUT";
    case QHttpServerRequest::Method::Delete: return "DELETE";
    case QHttpServerRequest::Method::Post: return "POST";
    case QHttpServerRequest::Method::Head: return "HEAD";
    case QHttpServerRequest::Method::Options: return "OPTIONS";
    case QHttpServerRequest::Method::Patch: return "PATCH";
    case QHttpServerRequest::Method::Connect: return "CONNECT";
    case QHttpServerRequest::Method::Trace: return "TRACE";
    default: return "UNKNOWN";
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
HttpListener::HttpListener(QObject* parent) : QAbstractHttpServer(parent)
{
}

/////////////////////////////////////////////////////////////////////////////////////////
bool HttpListener::listen(const QHostAddress& address, quint16 port)
{
  auto server = new QTcpServer(this);

  if (!server->listen(address, port) || !bind(server))
  {
    server->deleteLater();
    return false;
  }

  return true;
}

/////////////////////////////////////////////////////////////////////////////////////////
bool HttpListener::handleRequest(const QHttpServerRequest& request, QHttpServerResponder& responder)
{
  emit newRequest(request, new HttpResponse(std::move(responder), this));
  return true;
}

/////////////////////////////////////////////////////////////////////////////////////////
void HttpListener::missingHandler(const QHttpServerRequest& request, QHttpServerResponder& responder)
{
  Q_UNUSED(request);
  responder.write(QHttpServerResponder::StatusCode::NotFound);
}
