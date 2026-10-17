//
// Created by Tobias Hieta on 24/03/15.
//

#include "RemoteComponent.h"

#include <QDateTime>
#include <QXmlStreamWriter>
#include <QUrlQuery>

#include "QsLog.h"
#include "settings/SettingsComponent.h"
#include "utils/Utils.h"
#include "Version.h"

#define COMMAND_RESPONSE_TIMEOUT 30000

static QMap<QString, QString> g_resourceKeyMap = {
  { "Name", "title" },
  { "Resource-Identifier", "machineIdentifier" },
  { "Product", "product" },
  { "Version", "version" },
  { "Protocol-Version", "protocolVersion" },
  { "Protocol-Capabilities", "protocolCapabilities" },
  { "Device-Class", "deviceClass" }
};

static bool usableIdentifier(const QString& identifier)
{
  if (identifier.isEmpty() || identifier.size() > 256)
    return false;

  for (const QChar& c : identifier)
  {
    if (c.unicode() < 32 || c.unicode() == 127 || c.isSpace())
      return false;
  }

  return true;
}

static QMap<QString, QString> g_headerKeyMap = {
  { "Name", "X-Plex-Device-Name" },
  { "Resource-Identifier", "X-Plex-Client-Identifier" },
  { "Product", "X-Plex-Product" },
  { "Version", "X-Plex-Version" }
};

/////////////////////////////////////////////////////////////////////////////////////////
RemoteComponent::RemoteComponent(QObject* parent) : ComponentBase(parent), m_commandId(0)
{
  m_gdmManager = new GDMManager(this);
  m_networkAccessManager = new QNetworkAccessManager(this);
}

/////////////////////////////////////////////////////////////////////////////////////////
bool RemoteComponent::componentInitialize()
{
  m_gdmManager->startAnnouncing();

  // check for timed out subscribers
  m_subscriberTimer.setInterval(5000);
  connect(&m_subscriberTimer, &QTimer::timeout, this, &RemoteComponent::checkSubscribers);
  connect(&m_subscriberTimer, &QTimer::timeout, this, &RemoteComponent::checkResponses);
  m_subscriberTimer.start();

  // connect the network access stuff
  connect(m_networkAccessManager, &QNetworkAccessManager::finished, this, &RemoteComponent::timelineFinished);

  return true;
}

/////////////////////////////////////////////////////////////////////////////////////////
QVariantMap RemoteComponent::HeaderInformation()
{
  QVariantMap gdmInfo = GDMInformation();
  QVariantMap headerInfo;

  for(const QString& key : gdmInfo.keys())
  {
    if (g_headerKeyMap.contains(key))
      headerInfo[g_headerKeyMap[key]] = gdmInfo[key];
  }

  headerInfo["X-Plex-Platform"] = QSysInfo::productType();
  headerInfo["X-Plex-Platform-Version"] = QSysInfo::productVersion();
  
  return headerInfo;
}

/////////////////////////////////////////////////////////////////////////////////////////
QVariantMap RemoteComponent::ResourceInformation()
{
  QVariantMap gdmInfo = GDMInformation();
  QVariantMap resourceInfo;

  for(const QString& key : gdmInfo.keys())
  {
    if (g_resourceKeyMap.contains(key))
      resourceInfo[g_resourceKeyMap[key]] = gdmInfo[key];
  }

  resourceInfo["platform"] = QSysInfo::productType();
  resourceInfo["platformVersion"] = QSysInfo::productVersion();

  QString systemname = SettingsComponent::Get().value(SETTINGS_SECTION_SYSTEM, "systemname").toString();
  QString defaultSystemname = "PlexMediaPlayer";

  bool isSystemnameEmpty = systemname.isEmpty();
  bool isSystemnameDefault = systemname.compare(defaultSystemname) == 0;

  if (!isSystemnameEmpty && !isSystemnameDefault)
    resourceInfo["title"] = systemname;

  QLOG_DEBUG() << "Resolving system name: Host name:" << gdmInfo["Name"].toString();
  QLOG_DEBUG() << "Resolving system name: System name setting:" << systemname;
  QLOG_DEBUG() << "Resolving system name: Is system name setting empty value:" << isSystemnameEmpty;
  QLOG_DEBUG() << "Resolving system name: Is system name setting default value:" << isSystemnameDefault;
  QLOG_DEBUG() << "Resolving system name: Final value:" << resourceInfo["title"].toString();

  return resourceInfo;
};

/////////////////////////////////////////////////////////////////////////////////////////
QVariantMap RemoteComponent::GDMInformation()
{
  QVariantMap headers = {
    {"Name", Utils::sanitizeForHttpSeparators(Utils::ComputerName())},
    {"RawName", Utils::ComputerName()},
    {"Port", SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, "webserverport")},
    {"Version", Version::GetVersionString()},
    {"Product", "Plex Media Player"},
    {"Protocol", "plex"},
    {"Protocol-Version", "1"},
    {"Protocol-Capabilities", "navigation,playback,timeline,mirror,playqueues"},
    {"Device-Class", "pc"},
    {"Resource-Identifier", Utils::sanitizeForHttpSeparators(
                              SettingsComponent::Get().value(SETTINGS_SECTION_WEBCLIENT, "clientID").toString())}
  };

  return headers;
}

/////////////////////////////////////////////////////////////////////////////////////////
void RemoteComponent::handleResource(const QHttpServerRequest& request, HttpResponse* response)
{
  if (request.method() == QHttpServerRequest::Method::Get)
  {
    QVariantMap headers = ResourceInformation();

    QByteArray outputData;
    QXmlStreamWriter output(&outputData);
    output.setAutoFormatting(true);
    output.writeStartDocument();
    output.writeStartElement("MediaContainer");
    output.writeStartElement("Player");

    for(const QString& key : headers.keys())
      output.writeAttribute(key, headers[key].toString());

    output.writeEndElement();
    output.writeEndDocument();

    response->setStatusCode(HttpResponse::StatusCode::Ok);
    response->write(outputData);
    response->end();
  }
  else
  {
    response->setStatusCode(HttpResponse::StatusCode::MethodNotAllowed);
    response->end();
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
QVariantMap RemoteComponent::QueryToMap(const QUrl& url)
{
  QUrlQuery query(url);
  QVariantMap queryMap;

  const auto queryItems = query.queryItems(QUrl::FullyDecoded);
  for(const auto& stringPair : queryItems)
  {
    QString key = stringPair.first;
    QString value = stringPair.second;

    QVariantList l;
    if (queryMap.contains(key))
    {
      l = queryMap[key].toList();
      l.append(value);
    }
    else
    {
      l.append(value);
    }
    queryMap[key] = l;
  }

  return queryMap;
}

/////////////////////////////////////////////////////////////////////////////////////////
QVariantMap RemoteComponent::HeaderToMap(const QHttpHeaders& headers, const QUrl *url)
{
  QVariantMap variantMap;
  for(qsizetype i = 0; i < headers.size(); i++)
    variantMap.insert(headers.nameAt(i).toString().toLower(), headers.valueAt(i).toByteArray());

  // add any eventual X-Plex- param that could be in query parameters
  if (url)
  {
    QVariantMap paramsMap = QueryToMap(*url);
    for(const QString &key : paramsMap.keys())
    {
      QString paramKey = key.toLower();
      if ((paramKey.startsWith("x-plex-")) && (!variantMap.contains(paramKey)))
        variantMap.insert(paramKey, paramsMap[key].toList().value(0).toString());
    }
  }

  return variantMap;
}

/////////////////////////////////////////////////////////////////////////////////////////
void RemoteComponent::handleCommand(const QHttpServerRequest& request, HttpResponse* response)
{

  const QUrl url = request.url();
  QVariantMap queryMap = QueryToMap(url);
  QVariantMap headerMap = HeaderToMap(request.headers(), &url);
  QString identifier = headerMap["x-plex-client-identifier"].toString();

  response->addHeader("Access-Control-Allow-Origin", "*");
  response->addHeader("X-Plex-Client-Identifier",  SettingsComponent::Get().value(SETTINGS_SECTION_WEBCLIENT, "clientID").toByteArray());

  // handle CORS requests here
  if ((request.method() == QHttpServerRequest::Method::Options) && headerMap.contains("access-control-request-method"))
  {    
    response->addHeader("Content-Type", "text/plain");
    response->addHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS, DELETE, PUT, HEAD");
    response->addHeader("Access-Control-Max-Age", "1209600");
    response->addHeader("Connection", "close");

    if (headerMap.contains("access-control-request-headers"))
    {
      response->addHeader("Access-Control-Allow-Headers", headerMap.value("access-control-request-headers").toByteArray());
    }

    response->setStatusCode(HttpResponse::StatusCode::Ok);
    response->end();
    return;
  }

  // we want to handle the subscription events in the host
  // since we are going to handle the updating later.
  //
  if (request.url().path() == "/player/timeline/subscribe")
  {
    handleSubscription(request, response, false);
    return;
  }
  else if (request.url().path() == "/player/timeline/unsubscribe")
  {
    if (!usableIdentifier(identifier))
    {
      QLOG_WARN() << "Unusable client identifier in /player/timeline/unsubscribe";
      response->setStatusCode(HttpResponse::StatusCode::BadRequest);
      response->end();
      return;
    }

    bool allowed = false;
    {
      QMutexLocker lk(&m_subscriberLock);
      RemoteSubscriber* subscriber = m_subscriberMap.value(identifier, nullptr);
      allowed = subscriber && subscriber->remoteAddress() == request.remoteAddress();
    }

    if (!allowed)
    {
      QLOG_WARN() << "Refusing to unsubscribe" << identifier << "on behalf of another address";
      response->setStatusCode(HttpResponse::StatusCode::Forbidden);
      response->end();
      return;
    }

    subscriberRemove(identifier);
    response->setStatusCode(HttpResponse::StatusCode::Ok);
    response->end();
    return;
  }
  else if ((request.url().path() == "/player/timeline/poll"))
  {
    if (!usableIdentifier(identifier))
    {
      QLOG_WARN() << "Unusable client identifier in /player/timeline/poll";
      response->setStatusCode(HttpResponse::StatusCode::BadRequest);
      response->end();
      return;
    }

    QMutexLocker lk(&m_subscriberLock);
    if (!m_subscriberMap.contains(identifier))
    {
      lk.unlock();
      handleSubscription(request, response, true);
      lk.relock();
    }

    if (!m_subscriberMap.contains(identifier))
      return;

    RemotePollSubscriber *subscriber = dynamic_cast<RemotePollSubscriber *>(m_subscriberMap[identifier]);
    if (subscriber)
    {
      subscriber->reSubscribe();
      subscriber->setHTTPResponse(response);

      // if we don't have to wait, just ship the update right away
      // otherwise, this will wait until next update
      if (! (queryMap.contains("wait") && (queryMap["wait"].toList()[0].toInt() == 1)))
      {
        subscriber->sendUpdate();
      }
    }

    return;
  }


  // handle commandID
  if (!usableIdentifier(identifier) || !queryMap.contains("commandID"))
  {
    QLOG_WARN() << "Can't find a X-Plex-Client-Identifier header";
    response->setStatusCode(HttpResponse::StatusCode::NotAcceptable);
    response->end();
    return;
  }

  quint64 commandId = 0;
  {
    QMutexLocker lk(&m_responseLock);
    commandId = ++m_commandId;
    m_responseMap[commandId] = response;
    m_responseTime[commandId] = QDateTime::currentMSecsSinceEpoch();

    connect(response, &HttpResponse::done, this, &RemoteComponent::responseDone);
  }

  {
    QMutexLocker lk(&m_subscriberLock);
    if (!m_subscriberMap.contains(identifier))
    {
      QLOG_WARN() << "Failed to lock up subscriber" << identifier;
      response->setStatusCode(HttpResponse::StatusCode::NotAcceptable);
      response->end();
      return;
    }

    RemoteSubscriber* subscriber = m_subscriberMap[identifier];
    subscriber->setCommandId(m_commandId, queryMap["commandID"].toList()[0].toInt());
  }

  QVariantMap arg = {
    { "method", httpMethodName(request.method()) },
    { "headers", headerMap },
    { "path", request.url().path() },
    { "query", queryMap },
    { "commandID", m_commandId}
  };

  emit commandReceived(arg);
}

/////////////////////////////////////////////////////////////////////////////////////////
void RemoteComponent::responseDone()
{
  HttpResponse* response = qobject_cast<HttpResponse*>(sender());
  if (response)
  {
    QMutexLocker lk(&m_responseLock);

    for(auto it = m_responseMap.begin(); it != m_responseMap.end(); ++it)
    {
      if (it.value() == response)
      {
        m_responseTime.remove(it.key());
        m_responseMap.erase(it);
        break;
      }
    }
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
void RemoteComponent::commandResponse(const QVariantMap& responseArguments)
{
  // check for minimum requirements in the responseArguments
  if (!responseArguments.contains("commandID") ||
      !responseArguments.contains("responseCode"))
  {
    QLOG_WARN() << "responseArguments did not contain a commandId or responseCode";
    return;
  }

  quint64 commandId = responseArguments["commandID"].toULongLong();
  uint responseCode = responseArguments["responseCode"].toUInt();

  QMutexLocker lk(&m_responseLock);
  HttpResponse* response = m_responseMap.take(commandId);
  m_responseTime.remove(commandId);

  if (!response)
  {
    QLOG_WARN() << "Could not find responseId:" << commandId << " - maybe it was removed because of a timeout?";
    return;
  }

  // no need to hold the lock when we have changed m_responseMap
  lk.unlock();

  disconnect(response, &HttpResponse::done, this, &RemoteComponent::responseDone);

  // add headers if we have them
  if (responseArguments.contains("headers") && responseArguments["headers"].typeId() == QMetaType::QVariantMap)
  {
    QVariantMap headers = responseArguments["headers"].toMap();
      for(const QString& key : headers.keys())
        response->addHeader(key.toUtf8(), headers[key].toByteArray());
  }

  // write the response HTTP code
  response->setStatusCode((int)responseCode);

  // handle optional body argument
  if (responseArguments.contains("body") && responseArguments["body"].typeId() == QMetaType::QString)
    response->write(responseArguments["body"].toString().toUtf8());

  response->end();
}

/////////////////////////////////////////////////////////////////////////////////////////
void RemoteComponent::handleSubscription(const QHttpServerRequest& request, HttpResponse* response, bool poll)
{
  const QUrl url = request.url();
  QVariantMap headers = HeaderToMap(request.headers(), &url);

  // check for required headers
  if (!headers.contains("x-plex-client-identifier") ||
      (!headers.contains("x-plex-device-name")))
  {
    QLOG_ERROR() << "Missing X-Plex headers in /timeline/subscribe request";
    response->setStatusCode(HttpResponse::StatusCode::BadRequest);
    response->end();
    return;
  }

  const QString clientIdentifier = headers.value("x-plex-client-identifier").toString();
  const QString deviceName = Utils::sanitizeForHttpSeparators(headers.value("x-plex-device-name").toString());

  if (!usableIdentifier(clientIdentifier))
  {
    QLOG_ERROR() << "Unusable X-Plex-Client-Identifier in /timeline/subscribe request";
    response->setStatusCode(HttpResponse::StatusCode::BadRequest);
    response->end();
    return;
  }

  // check for required arguments
  QVariantMap query = QueryToMap(url);

  if (!query.contains("commandID") || ((!query.contains("port")) && !poll))
  {
    QLOG_ERROR() << "Missing arguments to /timeline/subscribe request";
    response->setStatusCode(HttpResponse::StatusCode::BadRequest);
    response->end();
    return;
  }

  QMutexLocker lk(&m_subscriberLock);
  RemoteSubscriber* subscriber = nullptr;

  if (m_subscriberMap.contains(clientIdentifier))
  {
    subscriber = m_subscriberMap[clientIdentifier];

    if (subscriber->remoteAddress() != request.remoteAddress())
    {
      QLOG_WARN() << "Refusing to refresh" << clientIdentifier << "from another address";
      lk.unlock();
      response->setStatusCode(HttpResponse::StatusCode::Forbidden);
      response->end();
      return;
    }

    QLOG_DEBUG() << "Refreshed subscriber:" << clientIdentifier;
    subscriber->reSubscribe();
  }
  else
  {
    if (poll)
    {
      QLOG_DEBUG() << "New poll subscriber:" << clientIdentifier << deviceName;
      subscriber = new RemotePollSubscriber(clientIdentifier, deviceName, response, this);
    }
    else
    {
      QUrl address;
      const QString protocol = query.contains("protocol") ? query["protocol"].toList().value(0).toString() : "http";
      const int port = query.contains("port") ? query["port"].toList().value(0).toInt() : 32400;

      if ((protocol != "http" && protocol != "https") || port < 1 || port > 65535)
      {
        QLOG_ERROR() << "Refusing a subscription to" << protocol << port;
        response->setStatusCode(HttpResponse::StatusCode::BadRequest);
        response->end();
        return;
      }

      address.setScheme(protocol);
      address.setHost(request.remoteAddress().toString());
      address.setPort(port);

      QLOG_DEBUG() << "New subscriber:" << clientIdentifier << deviceName << address.toString();
      subscriber = new RemoteSubscriber(clientIdentifier, deviceName, address, this);
    }

    subscriber->setRemoteAddress(request.remoteAddress());
    m_subscriberMap[clientIdentifier] = subscriber;

    // if it's our first controller, we notify web for subscription
    if (m_subscriberMap.size() == 1)
    {
      QLOG_DEBUG() << "First subscriber added, subscribing to web";
      subscribeToWeb(true);
    }
  }

  subscriber->setCommandId(m_commandId, query["commandID"].toList()[0].toInt());

  if (!poll)
  {
    subscriber->sendUpdate();

    response->setStatusCode(HttpResponse::StatusCode::Ok);
    response->end();
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
void RemoteComponent::subscribeToWeb(bool subscribe)
{
  QVariantMap arg = {
    { "method", "GET" },
    { "path",  "/player/timeline/" +  (subscribe ? QLatin1String("subscribe") : QLatin1String("unsubscribe")) },
    { "commandID", m_commandId}
  };

  emit commandReceived(arg);
}

/////////////////////////////////////////////////////////////////////////////////////////
void RemoteComponent::checkSubscribers()
{
  QMutexLocker lk(&m_subscriberLock);
  QList<RemoteSubscriber*> subsToRemove;
  for(RemoteSubscriber* subscriber : m_subscriberMap.values())
  {
    // was it more than 10 seconds since this client checked in last?
    if (subscriber->lastSubscribe() > 90 * 1000)
    {
      QLOG_DEBUG() << "more than 10 seconds since we heard from:" << subscriber->deviceName() << "- unsubscribing..";
      subsToRemove << subscriber;
    }
  }

  lk.unlock();

  for(RemoteSubscriber* sub : subsToRemove)
    subscriberRemove(sub->clientIdentifier());
}

/////////////////////////////////////////////////////////////////////////////////////////
void RemoteComponent::checkResponses()
{
  QList<HttpResponse*> responsesToEnd;

  {
    QMutexLocker lk(&m_responseLock);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    for(auto it = m_responseTime.begin(); it != m_responseTime.end();)
    {
      if (now - it.value() < COMMAND_RESPONSE_TIMEOUT)
      {
        ++it;
        continue;
      }

      HttpResponse* response = m_responseMap.value(it.key(), nullptr);
      if (response)
      {
        QLOG_WARN() << "No response to command" << it.key() << "- timing it out";
        disconnect(response, &HttpResponse::done, this, &RemoteComponent::responseDone);
        responsesToEnd << response;
      }

      m_responseMap.remove(it.key());
      it = m_responseTime.erase(it);
    }
  }

  for(HttpResponse* response : responsesToEnd)
  {
    response->setStatusCode(HttpResponse::StatusCode::GatewayTimeout);
    response->end();
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
QNetworkAccessManager* RemoteComponent::getNetworkAccessManager()
{
  // we might want to set common options here.
  return m_networkAccessManager;
}


/////////////////////////////////////////////////////////////////////////////////////////
void RemoteComponent::timelineFinished(QNetworkReply* reply)
{
  QString identifier = reply->request().attribute(QNetworkRequest::User).toString();

  // ignore requests with no identifier
  if (identifier.isEmpty())
    return;

  QMutexLocker lk(&m_subscriberLock);
  if (!m_subscriberMap.contains(identifier))
  {
    QLOG_WARN() << "Got a networkreply with a identifier we don't know about:" << identifier;
    return;
  }

  RemoteSubscriber* sub = m_subscriberMap[identifier];
  lk.unlock();

  sub->timelineFinished(reply);
}

/////////////////////////////////////////////////////////////////////////////////////////
void RemoteComponent::subscriberRemove(const QString& identifier)
{
  QMutexLocker lk(&m_subscriberLock);
  if (!m_subscriberMap.contains(identifier))
  {
    QLOG_ERROR() << "Can't remove client:" << identifier << "since we don't know about it.";
    return;
  }

  RemoteSubscriber* subscriber = m_subscriberMap[identifier];
  m_subscriberMap.remove(identifier);
  subscriber->deleteLater();

  // if it's our first controller, we notify web for subscription
  if (m_subscriberMap.size() == 0)
  {
    QLOG_DEBUG() << "Last subscriber removed, unsubscribing from web";
    subscribeToWeb(false);
  }
  QLOG_DEBUG() << "Removed subscriber:" << identifier;
}

/////////////////////////////////////////////////////////////////////////////////////////
void RemoteComponent::timelineUpdate(quint64 commandID, const QString& timeline)
{
  QMutexLocker lk(&m_subscriberLock);

  for(RemoteSubscriber* subscriber : m_subscriberMap.values())
  {
    subscriber->queueTimeline(commandID, timeline.toUtf8());
    subscriber->sendUpdate();
  }
}
