#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include <QObject>
#include <QString>
#include <QMimeDatabase>
#include <QVariant>

#include "HttpListener.h"

class HttpServer : public QObject
{
Q_OBJECT
public:
  explicit HttpServer(QObject* parent);
  bool start();

private slots:
  void handleRequest(const QHttpServerRequest& request, HttpResponse* response);
  void handleWebClientRequest(const QHttpServerRequest& request, HttpResponse* response);
  void handleResource(const QHttpServerRequest& request, HttpResponse* response);
  void handleRemoteController(const QHttpServerRequest& request, HttpResponse* response);
  void handleFilesRequest(const QHttpServerRequest& request, HttpResponse* response);
  void writeError(HttpResponse* response, HttpResponse::StatusCode errorCode);

private:
  bool writeFile(const QString& file, HttpResponse* response);
  void handleSoundsRequest(const QHttpServerRequest& request, HttpResponse* response);

  bool handleEmbeddedServerRequest(const QHttpServerRequest& request, HttpResponse* response);
  void writeMediaContainer(const QHttpServerRequest& request, HttpResponse* response,
                           const QVariantMap& attributes,
                           const QString& childName = QString(),
                           const QVariantList& children = QVariantList());
  QString machineIdentifier() const;

  HttpListener* m_server;
  QString m_baseUrl;
  quint16 m_port;
  QMimeDatabase m_mime;
};

#endif // HTTPSERVER_H
