#include "HttpResponse.h"

#include <QHttpHeaders>

#define CANCEL_POLL_INTERVAL 1000

/////////////////////////////////////////////////////////////////////////////////////////
HttpResponse::HttpResponse(QHttpServerResponder&& responder, QObject* parent)
  : QObject(parent), m_responder(std::move(responder)), m_statusCode((int)StatusCode::Ok)
{
  m_cancelTimer.setInterval(CANCEL_POLL_INTERVAL);
  connect(&m_cancelTimer, &QTimer::timeout, this, &HttpResponse::checkCanceled);
  m_cancelTimer.start();
}

/////////////////////////////////////////////////////////////////////////////////////////
void HttpResponse::setStatusCode(StatusCode code)
{
  m_statusCode = (int)code;
}

/////////////////////////////////////////////////////////////////////////////////////////
void HttpResponse::setStatusCode(int code)
{
  m_statusCode = code;
}

/////////////////////////////////////////////////////////////////////////////////////////
void HttpResponse::addHeader(const QByteArray& key, const QByteArray& value)
{
  m_headers.append({ key.toLower(), value });
}

/////////////////////////////////////////////////////////////////////////////////////////
void HttpResponse::write(const QByteArray& data)
{
  m_body.append(data);
}

/////////////////////////////////////////////////////////////////////////////////////////
void HttpResponse::end(const QByteArray& data)
{
  if (!m_responder)
    return;

  m_body.append(data);

  QHttpHeaders headers;
  for (const auto& header : m_headers)
  {
    // the protocol handler writes these itself
    if (header.first == "content-length" || header.first == "connection" ||
        header.first == "keep-alive" || header.first == "transfer-encoding")
      continue;

    headers.append(header.first, header.second);
  }

  m_responder->write(m_body, headers, (StatusCode)m_statusCode);
  finish();
}

/////////////////////////////////////////////////////////////////////////////////////////
void HttpResponse::checkCanceled()
{
  if (m_responder && m_responder->isResponseCanceled())
    finish();
}

/////////////////////////////////////////////////////////////////////////////////////////
void HttpResponse::finish()
{
  m_cancelTimer.stop();
  m_responder.reset();

  emit done();

  deleteLater();
}
