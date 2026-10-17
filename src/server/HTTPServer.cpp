#include "HTTPServer.h"

#include <QFile>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>
#include <QXmlStreamWriter>

#include "QsLog.h"
#include "utils/Utils.h"
#include "settings/SettingsComponent.h"
#include "remote/RemoteComponent.h"
#include "Paths.h"

#define WEB_CLIENT_PATH "/web/tv"
#define NOTIFICATIONS_PATH "/:/websockets/notifications"

static bool pathHasPrefix(const QString& path, const QString& prefix)
{
  if (!path.startsWith(prefix))
    return false;

  return path.size() == prefix.size() || path.at(prefix.size()) == '/';
}

/////////////////////////////////////////////////////////////////////////////////////////
HttpServer::HttpServer(QObject* parent) : QObject(parent)
{
  m_server = new HttpListener(this);
  m_loopbackServer = nullptr;
  m_baseUrl = ":/konvergo";
  m_port = (quint16)SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, "webserverport").toUInt();
  m_loopbackPort =
    (quint16)SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, "embeddedServerPort").toUInt();
}

/////////////////////////////////////////////////////////////////////////////////////////
bool HttpServer::start()
{
  connect(m_server, &HttpListener::newRequest, this, &HttpServer::handleRequest);

  if (SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, "embeddedServer").toBool())
    m_server->acceptWebSocketPath(NOTIFICATIONS_PATH);

  const bool remote = SettingsComponent::Get().remoteControlEnabled();
  const QHostAddress address = remote ? QHostAddress(QHostAddress::AnyIPv4)
                                      : QHostAddress(QHostAddress::LocalHost);

  if (!m_server->listen(address, m_port))
  {
    QLOG_WARN() << "Failed to listen to remote control web server. Remote controlling from apps disabled.";
    return false;
  }

  QLOG_DEBUG() << "Listening on" << qPrintable(address.toString()) << "port:" << m_port;

  if (SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, "embeddedServer").toBool()
      && m_loopbackPort && m_loopbackPort != m_port)
  {
    m_loopbackServer = new HttpListener(this);
    m_loopbackServer->acceptWebSocketPath(NOTIFICATIONS_PATH);
    connect(m_loopbackServer, &HttpListener::newRequest, this, &HttpServer::handleLoopbackRequest);

    if (m_loopbackServer->listen(QHostAddress(QHostAddress::LocalHost), m_loopbackPort))
      QLOG_DEBUG() << "Listening on 127.0.0.1 port:" << m_loopbackPort;
    else
      QLOG_WARN() << "Failed to listen on 127.0.0.1 port:" << m_loopbackPort;
  }

  return true;
}

/////////////////////////////////////////////////////////////////////////////////////////
void HttpServer::writeError(HttpResponse* response, HttpResponse::StatusCode errorCode)
{
  QByteArray errorStyle = "<style>h1 {color:red;}</style>";
  response->setStatusCode(errorCode);
  response->write(errorStyle);

  QByteArray error;
  switch (errorCode)
  {
    case HttpResponse::StatusCode::Forbidden:
      error = "Access to file denied. You might want to check permissions";
      break;
    case HttpResponse::StatusCode::NotFound:
      error = "Could not find that file. Install might be broken?";
      break;
    case HttpResponse::StatusCode::MethodNotAllowed:
      error = "That method is not allowed.";
      break;
    case HttpResponse::StatusCode::NotImplemented:
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
bool HttpServer::writeFile(const QString& file, HttpResponse* response)
{
  QLOG_DEBUG() << "Going to request file:" << qPrintable(file);
  if (QFile::exists(file))
  {
    QFile fp(file);
    if (fp.open(QFile::ReadOnly))
    {
      auto mime = m_mime.mimeTypeForFile(QFileInfo(fp));

      response->setStatusCode(HttpResponse::StatusCode::Ok);
      response->addHeader("Content-Type", mime.name().toUtf8());
      response->write(fp.readAll());

      fp.close();
      return true;
    }
    else
    {
      writeError(response, HttpResponse::StatusCode::Forbidden);
      return false;
    }
  }
  else
  {
    writeError(response, HttpResponse::StatusCode::NotFound);
    return false;
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
void HttpServer::handleWebClientRequest(const QHttpServerRequest& request, HttpResponse* response)
{
  switch (request.method())
  {
    case QHttpServerRequest::Method::Get:
    {
      // cut the WEB_CLIENT_PATH prefix
      QString relativeUrl = request.url().path();
      if (pathHasPrefix(relativeUrl, WEB_CLIENT_PATH))
        relativeUrl = relativeUrl.mid(sizeof(WEB_CLIENT_PATH) - 1);

      const QString rUrl = QDir::cleanPath(m_baseUrl + "/" + relativeUrl);
      if (rUrl != m_baseUrl && !rUrl.startsWith(m_baseUrl + "/"))
      {
        writeError(response, HttpResponse::StatusCode::Forbidden);
        QLOG_WARN() << "Refusing web client path" << relativeUrl;
        break;
      }

      writeFile(rUrl, response);
      break;
    }

    default:
    {
      writeError(response, HttpResponse::StatusCode::MethodNotAllowed);
      QLOG_WARN() << "Method" << qPrintable(httpMethodName(request.method())) << "is not supported";
    }
  }

  response->end();
}

/////////////////////////////////////////////////////////////////////////////////////////
void HttpServer::handleResource(const QHttpServerRequest& request, HttpResponse* response)
{
  RemoteComponent::Get().handleResource(request, response);
}

/////////////////////////////////////////////////////////////////////////////////////////
void HttpServer::handleRemoteController(const QHttpServerRequest& request, HttpResponse* response)
{
  RemoteComponent::Get().handleCommand(request, response);
}

/////////////////////////////////////////////////////////////////////////////////////////
void HttpServer::handleFilesRequest(const QHttpServerRequest& request, HttpResponse* response)
{
  if (request.url().path() == "/files/qwebchannel.js")
    writeFile(":/qtwebchannel/qwebchannel.js", response);
  else
    writeError(response, HttpResponse::StatusCode::NotFound);

  response->end();
}

/////////////////////////////////////////////////////////////////////////////////////////
void HttpServer::handleSoundsRequest(const QHttpServerRequest& request, HttpResponse* response)
{
  auto sound = QFileInfo(request.url().path()).fileName();
  auto soundPath = Paths::soundsPath(sound);

  if (soundPath.isEmpty())
    writeError(response, HttpResponse::StatusCode::NotFound);
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

void HttpServer::writeMediaContainer(const QHttpServerRequest& request, HttpResponse* response,
                                     const QVariantMap& attributes)
{
  const bool wantsJson = request.value("accept").contains("json");

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
    QXmlStreamWriter writer(&body);
    writer.writeStartDocument(QStringLiteral("1.0"));
    writer.writeEmptyElement(QStringLiteral("MediaContainer"));
    for (auto it = attributes.constBegin(); it != attributes.constEnd(); ++it)
      writer.writeAttribute(it.key(), it.value().toString());
    writer.writeEndDocument();
  }

  response->setStatusCode(HttpResponse::StatusCode::Ok);
  response->addHeader("Content-Type", wantsJson ? "application/json" : "text/xml;charset=utf-8");
  response->addHeader("Access-Control-Allow-Origin", "*");
  response->end(body);
}

bool HttpServer::handleEmbeddedServerRequest(const QHttpServerRequest& request, HttpResponse* response)
{
  if (!SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, "embeddedServer").toBool())
    return false;

  const QString path = request.url().path();

  if (path != "/" && path != "/identity" && path != "/media/providers"
      && path != "/media/subscriptions" && path != "/media/providers/remote/status")
    return false;

  if (request.method() == QHttpServerRequest::Method::Options)
  {
    response->setStatusCode(HttpResponse::StatusCode::Ok);
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "*");
    response->addHeader("Access-Control-Max-Age", "86400");
    response->end();
    return true;
  }

  if (path == "/")
  {
    writeMediaContainer(request, response,
                        { { "size", 0 },
                          { "machineIdentifier", machineIdentifier() },
                          { "version", QCoreApplication::applicationVersion() },
                          { "allowSync", 0 },
                          { "mediaProviders", 1 } });
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

    response->setStatusCode(HttpResponse::StatusCode::Ok);
    response->addHeader("Content-Type", "text/xml;charset=utf-8");
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->end(body);
    return true;
  }

  return false;
}

/////////////////////////////////////////////////////////////////////////////////////////
void HttpServer::handleLoopbackRequest(const QHttpServerRequest& request, HttpResponse* response)
{
  QLOG_DEBUG() << "Incoming request to:" << request.url().toString() << "from" << request.remoteAddress().toString();

  if (handleEmbeddedServerRequest(request, response))
    return;

  writeError(response, HttpResponse::StatusCode::NotFound);
  response->end();
}

/////////////////////////////////////////////////////////////////////////////////////////
void HttpServer::handleRequest(const QHttpServerRequest& request, HttpResponse* response)
{
  QLOG_DEBUG() << "Incoming request to:" << request.url().toString() << "from" << request.remoteAddress().toString();

  QString path = request.url().path();
  if (handleEmbeddedServerRequest(request, response))
  {
  }
  else if (pathHasPrefix(path, WEB_CLIENT_PATH))
  {
    handleWebClientRequest(request, response);
  }
  else if (pathHasPrefix(path, "/resources"))
  {
    handleResource(request, response);
  }
  else if (pathHasPrefix(path, "/player"))
  {
    handleRemoteController(request, response);
  }
  else if (pathHasPrefix(path, "/files"))
  {
    handleFilesRequest(request, response);
  }
  else if (pathHasPrefix(path, "/sounds"))
  {
    handleSoundsRequest(request, response);
  }
  else if (path == "/")
  {
    response->setStatusCode(HttpResponse::StatusCode::Ok);
    response->end("You should not be here :-)");
  }
  else
  {
    writeError(response, HttpResponse::StatusCode::NotFound);
    response->end();
  }
}
