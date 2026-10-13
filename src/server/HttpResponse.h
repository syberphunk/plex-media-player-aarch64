#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <optional>

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPair>
#include <QTimer>

#ifdef BadRequest
#undef BadRequest
#endif

#include <QtHttpServer/QHttpServerResponder>

class HttpResponse : public QObject
{
  Q_OBJECT

public:
  using StatusCode = QHttpServerResponder::StatusCode;

  explicit HttpResponse(QHttpServerResponder&& responder, QObject* parent = nullptr);

  void setStatusCode(StatusCode code);
  void setStatusCode(int code);

  void addHeader(const QByteArray& key, const QByteArray& value);

  void write(const QByteArray& data);
  void end(const QByteArray& data = QByteArray());

signals:
  void done();

private:
  void checkCanceled();
  void finish();

  std::optional<QHttpServerResponder> m_responder;
  QList<QPair<QByteArray, QByteArray>> m_headers;
  QByteArray m_body;
  int m_statusCode;
  QTimer m_cancelTimer;
};

#endif
