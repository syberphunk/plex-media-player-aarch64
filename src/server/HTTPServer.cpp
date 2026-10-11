#include "HTTPServer.h"

#include <QFile>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>

#include "QsLog.h"
#include "utils/Utils.h"
#include "settings/SettingsComponent.h"
#include "remote/RemoteComponent.h"
#include "Paths.h"

#define WEB_CLIENT_PATH "/web/tv"

/////////////////////////////////////////////////////////////////////////////////////////
HttpServer::HttpServer(QObject* parent) : QObject(parent)
{
  m_server = new QHttpServer(this);
  m_baseUrl = ":/konvergo";
  m_port = (quint16)SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, "webserverport").toUInt();
}

/////////////////////////////////////////////////////////////////////////////////////////
bool HttpServer::start()
{
  connect(m_server, &QHttpServer::newRequest, this, &HttpServer::handleRequest);

  const bool remote = SettingsComponent::Get().remoteControlEnabled();
  const QHostAddress address = remote ? QHostAddress(QHostAddress::AnyIPv4)
                                      : QHostAddress(QHostAddress::LocalHost);

  if (!m_server->listen(address, m_port))
  {
    QLOG_WARN() << "Failed to listen to remote control web server. Remote controlling from apps disabled.";
    return false;
  }

  QLOG_DEBUG() << "Listening on" << qPrintable(address.toString()) << "port:" << m_port;
  return true;
}

/////////////////////////////////////////////////////////////////////////////////////////
void HttpServer::writeError(QHttpResponse* response, qhttp::TStatusCode errorCode)
{
  QByteArray errorStyle = "<style>h1 {color:red;}</style>";
  response->setStatusCode(errorCode);
  response->write(errorStyle);

  QByteArray error;
  switch (errorCode)
  {
    case qhttp::ESTATUS_FORBIDDEN:
      error = "Access to file denied. You might want to check permissions";
      break;
    case qhttp::ESTATUS_NOT_FOUND:
      error = "Could not find that file. Install might be broken?";
      break;
    case qhttp::ESTATUS_METHOD_NOT_ALLOWED:
      error = "That method is not allowed.";
      break;
    case qhttp::ESTATUS_NOT_IMPLEMENTED:
      error = "This request is not yet implemented";
      break;
    default:
      error = "Generic error, something went wrong<tm>";
      break;
  }
  response->write("<h1>");
  response->write(error);
  response->write("</h1>");
}

/////////////////////////////////////////////////////////////////////////////////////////
bool HttpServer::writeFile(const QString& file, QHttpResponse* response)
{
  QLOG_DEBUG() << "Going to request file:" << qPrintable(file);
  if (QFile::exists(file))
  {
    QFile fp(file);
    if (fp.open(QFile::ReadOnly))
    {
      auto mime = m_mime.mimeTypeForFile(QFileInfo(fp));

      response->setStatusCode(qhttp::ESTATUS_OK);
      response->addHeader("Content-Type", mime.name().toUtf8());
      response->addHeader("Content-Length", QByteArray::number(fp.size()));
      response->write(fp.readAll());

      fp.close();
      return true;
    }
    else
    {
      writeError(response, qhttp::ESTATUS_FORBIDDEN);
      return false;
    }
  }
  else
  {
    writeError(response, qhttp::ESTATUS_NOT_FOUND);
    return false;
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
void HttpServer::handleWebClientRequest(QHttpRequest* request, QHttpResponse* response)
{
  switch (request->method())
  {
    case qhttp::EHTTP_GET:
    {
      // cut the WEB_CLIENT_PATH prefix
      QString relativeUrl = request->url().path();
      relativeUrl.replace(WEB_CLIENT_PATH, "");
      QString rUrl = m_baseUrl + relativeUrl;

      writeFile(rUrl, response);
      break;
    }

    default:
    {
      writeError(response, qhttp::ESTATUS_METHOD_NOT_ALLOWED);
      QLOG_WARN() << "Method" << qPrintable(request->methodString()) << "is not supported";
    }
  }

  response->end();
}

/////////////////////////////////////////////////////////////////////////////////////////
void HttpServer::handleResource(QHttpRequest* request, QHttpResponse* response)
{
  RemoteComponent::Get().handleResource(request, response);
}

/////////////////////////////////////////////////////////////////////////////////////////
void HttpServer::handleRemoteController(QHttpRequest* request, QHttpResponse* response)
{
  RemoteComponent::Get().handleCommand(request, response);
}

/////////////////////////////////////////////////////////////////////////////////////////
void HttpServer::handleFilesRequest(QHttpRequest* request, QHttpResponse* response)
{
  if (request->url().path() == "/files/qwebchannel.js")
    writeFile(":/qtwebchannel/qwebchannel.js", response);
  else
    writeError(response, qhttp::ESTATUS_NOT_FOUND);

  response->end();
}

/////////////////////////////////////////////////////////////////////////////////////////
void HttpServer::handleSoundsRequest(QHttpRequest* request, QHttpResponse* response)
{
  auto sound = QFileInfo(request->url().path()).fileName();
  auto soundPath = Paths::soundsPath(sound);

  if (soundPath.isEmpty())
    writeError(response, qhttp::ESTATUS_NOT_FOUND);
  else
    writeFile(Paths::soundsPath(sound), response);

  response->end();
}

QString HttpServer::machineIdentifier() const
{
  auto seed = SettingsComponent::Get()
                .value(SETTINGS_SECTION_WEBCLIENT, "clientID")
                .toString()
                .toUtf8();

  if (seed.isEmpty())
    seed = QSysInfo::machineUniqueId();

  return QCryptographicHash::hash("pmp-embedded-server:" + seed,
                                  QCryptographicHash::Sha1).toHex();
}

void HttpServer::writeMediaContainer(QHttpRequest* request, QHttpResponse* response,
                                     const QVariantMap& attributes)
{
  const bool wantsJson = request->headers().value("accept").contains("json");

  QByteArray body;
  if (wantsJson)
  {
    QJsonObject container;
    for (auto it = attributes.constBegin(); it != attributes.constEnd(); ++it)
      container.insert(it.key(), QJsonValue::fromVariant(it.value()));

    QJsonObject root;
    root.insert("MediaContainer", container);
    body = QJsonDocument(root).toJson(QJsonDocument::Compact);
  }
  else
  {
    body = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<MediaContainer";
    for (auto it = attributes.constBegin(); it != attributes.constEnd(); ++it)
      body += " " + it.key().toUtf8() + "=\"" + it.value().toString().toUtf8() + "\"";
    body += " />\n";
  }

  response->setStatusCode(qhttp::ESTATUS_OK);
  response->addHeader("Content-Type", wantsJson ? "application/json" : "text/xml;charset=utf-8");
  response->addHeader("Content-Length", QByteArray::number(body.size()));
  response->addHeader("Access-Control-Allow-Origin", "*");
  response->end(body);
}

bool HttpServer::handleEmbeddedServerRequest(QHttpRequest* request, QHttpResponse* response)
{
  if (!SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, "embeddedServer").toBool())
    return false;

  const QString path = request->url().path();

  if (path != "/identity" && path != "/media/providers" && path != "/media/subscriptions"
      && path != "/media/providers/remote/status")
    return false;

  if (request->method() == qhttp::EHTTP_OPTIONS)
  {
    response->setStatusCode(qhttp::ESTATUS_OK);
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "*");
    response->addHeader("Access-Control-Max-Age", "86400");
    response->addHeader("Content-Length", "0");
    response->end();
    return true;
  }

  if (path == "/media/providers")
  {
    writeMediaContainer(request, response,
                        { { "size", 0 },
                          { "machineIdentifier", machineIdentifier() },
                          { "allowSync", 0 } });
    return true;
  }

  if (path == "/media/subscriptions")
  {
    writeMediaContainer(request, response, { { "size", 0 } });
    return true;
  }

  if (path == "/media/providers/remote/status")
  {
    writeMediaContainer(request, response, { { "size", 0 } });
    return true;
  }

  if (path == "/identity")
  {
    QByteArray body = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                      "<MediaContainer machineIdentifier=\""
                      + machineIdentifier().toUtf8()
                      + "\" version=\"" + QCoreApplication::applicationVersion().toUtf8()
                      + "\" />\n";

    response->setStatusCode(qhttp::ESTATUS_OK);
    response->addHeader("Content-Type", "text/xml;charset=utf-8");
    response->addHeader("Content-Length", QByteArray::number(body.size()));
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->end(body);
    return true;
  }

  return false;
}

/////////////////////////////////////////////////////////////////////////////////////////
void HttpServer::handleRequest(QHttpRequest* request, QHttpResponse* response)
{
  QLOG_DEBUG() << "Incoming request to:" << request->url().toString() << "from" << request->remoteAddress();

  QString path = request->url().path();
  if (handleEmbeddedServerRequest(request, response))
  {
  }
  else if (path.startsWith(WEB_CLIENT_PATH))
  {
    handleWebClientRequest(request, response);
  }
  else if (path.startsWith("/resources"))
  {
    handleResource(request, response);
  }
  else if (path.startsWith("/player"))
  {
    handleRemoteController(request, response);
  }
  else if (path.startsWith("/files"))
  {
    handleFilesRequest(request, response);
  }
  else if (path.startsWith("/sounds"))
  {
    handleSoundsRequest(request, response);
  }
  else if (path == "/")
  {
    response->setStatusCode(qhttp::ESTATUS_OK);
    response->end("You should not be here :-)");
  }
  else
  {
    writeError(response, qhttp::ESTATUS_NOT_FOUND);
    response->end();
  }
}
