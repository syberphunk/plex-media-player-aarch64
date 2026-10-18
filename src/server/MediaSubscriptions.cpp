#include "MediaSubscriptions.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSslError>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QXmlStreamWriter>

#include <QtHttpServer/QHttpServerRequest>

#include "QsLog.h"
#include "Paths.h"

#define ACTIVITY_REFRESH "provider.subscription.refresh"
#define ACTIVITY_GRAB "grabber.grab"

#define ELEM QStringLiteral("_elem")
#define KIDS QStringLiteral("_kids")
#define SINGLE QStringLiteral("_single")

namespace
{
  struct TypeEntry
  {
    int id;
    const char* name;
  };

  const TypeEntry g_types[] = { { 1, "movie" },  { 2, "show" },  { 3, "season" },
                                { 4, "episode" }, { 8, "artist" }, { 9, "album" },
                                { 10, "track" }, { 12, "clip" },  { 13, "photo" },
                                { 21, "audiobook" } };

  QString typeName(int id)
  {
    for (const TypeEntry& entry : g_types)
      if (entry.id == id)
        return QString::fromLatin1(entry.name);

    return QString();
  }

  int typeId(const QString& name)
  {
    for (const TypeEntry& entry : g_types)
      if (name == QLatin1String(entry.name))
        return entry.id;

    return 0;
  }

  void splitSource(const QString& source, QString& machine, QString& provider)
  {
    QString rest = source;
    if (rest.startsWith("server://"))
      rest = rest.mid(9);

    const int slash = rest.indexOf('/');
    if (slash < 0)
    {
      machine = rest;
      provider = QStringLiteral("com.plexapp.plugins.library");
      return;
    }

    machine = rest.left(slash);
    provider = rest.mid(slash + 1);
  }

  QString text(const QVariant& value)
  {
    if (value.typeId() == QMetaType::Bool)
      return value.toBool() ? QStringLiteral("1") : QStringLiteral("0");

    return value.toString();
  }

  QJsonObject nodeToJson(const QVariantMap& node)
  {
    QJsonObject object;
    for (auto it = node.constBegin(); it != node.constEnd(); ++it)
    {
      if (it.key().startsWith('_'))
        continue;

      object.insert(it.key(), QJsonValue::fromVariant(it.value()));
    }

    QStringList order;
    QHash<QString, QJsonArray> groups;
    QHash<QString, bool> singles;

    for (const QVariant& child : node.value(KIDS).toList())
    {
      const QVariantMap map = child.toMap();
      const QString name = map.value(ELEM).toString();

      if (!groups.contains(name))
        order.append(name);

      groups[name].append(nodeToJson(map));
      singles[name] = map.value(SINGLE).toBool();
    }

    for (const QString& name : order)
    {
      const QJsonArray items = groups.value(name);
      if (singles.value(name) && items.size() == 1)
        object.insert(name, items.at(0));
      else
        object.insert(name, items);
    }

    return object;
  }

  bool wantsJson(const QHttpServerRequest& request)
  {
    return !request.value("accept").contains("xml");
  }

  void nodeToXml(QXmlStreamWriter& writer, const QVariantMap& node)
  {
    const QVariantList children = node.value(KIDS).toList();
    const QString name = node.value(ELEM).toString();

    if (children.isEmpty())
      writer.writeEmptyElement(name);
    else
      writer.writeStartElement(name);

    for (auto it = node.constBegin(); it != node.constEnd(); ++it)
    {
      if (it.key().startsWith('_'))
        continue;

      writer.writeAttribute(it.key(), text(it.value()));
    }

    for (const QVariant& child : children)
      nodeToXml(writer, child.toMap());

    if (!children.isEmpty())
      writer.writeEndElement();
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
MediaSubscriptions::MediaSubscriptions(const QString& machineIdentifier, QObject* parent)
  : QObject(parent), m_machineIdentifier(machineIdentifier)
{
  m_network = new QNetworkAccessManager(this);
  m_network->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);

  load();
}

/////////////////////////////////////////////////////////////////////////////////////////
QVariantMap MediaSubscriptions::node(const QString& name, const QVariantMap& attributes,
                                     const QVariantList& children)
{
  QVariantMap result = attributes;
  result.insert(ELEM, name);
  if (!children.isEmpty())
    result.insert(KIDS, children);

  return result;
}

/////////////////////////////////////////////////////////////////////////////////////////
void MediaSubscriptions::writeContainer(const QHttpServerRequest& request, HttpResponse* response,
                                        const QVariantMap& attributes, const QVariantList& children)
{
  writeContainer(wantsJson(request), response, attributes, children);
}

/////////////////////////////////////////////////////////////////////////////////////////
void MediaSubscriptions::writeContainer(bool wantsJson, HttpResponse* response,
                                        const QVariantMap& attributes, const QVariantList& children)
{
  QVariantMap container = node(QStringLiteral("MediaContainer"), attributes, children);

  QByteArray body;
  if (wantsJson)
  {
    QJsonObject root;
    root.insert(QStringLiteral("MediaContainer"), nodeToJson(container));
    body = QJsonDocument(root).toJson(QJsonDocument::Compact);
  }
  else
  {
    QXmlStreamWriter writer(&body);
    writer.writeStartDocument(QStringLiteral("1.0"));
    nodeToXml(writer, container);
    writer.writeEndDocument();
  }

  response->setStatusCode(HttpResponse::StatusCode::Ok);
  response->addHeader("Content-Type", wantsJson ? "application/json" : "text/xml;charset=utf-8");
  response->addHeader("Access-Control-Allow-Origin", "*");
  response->end(body);
}

/////////////////////////////////////////////////////////////////////////////////////////
bool MediaSubscriptions::claims(const QString& path) const
{
  if (path == "/media/providers" || path == "/media/providers/remote/sync"
      || path == "/media/subscriptions" || path == "/media/subscriptions/template"
      || path == "/media/subscriptions/process" || path == "/activities"
      || path == "/updater/check")
    return true;

  if (path.startsWith("/media/subscriptions/") || path.startsWith("/activities/"))
    return true;

  if (path.startsWith("/media/providers/")
      && (path.endsWith("/connection") || path.contains("/media/subscriptions/mapping/")))
    return true;

  return false;
}

/////////////////////////////////////////////////////////////////////////////////////////
bool MediaSubscriptions::handle(const QHttpServerRequest& request, HttpResponse* response)
{
  const QString path = request.url().path();

  if (path.endsWith("/connection") && path.startsWith("/media/providers/"))
    return handleConnection(request, response);

  if (path == "/media/providers")
    return handleProviders(request, response);

  if (path.contains("/media/subscriptions/mapping/"))
    return handleMapping(request, response);

  if (path == "/media/subscriptions/template")
    return handleTemplate(request, response);

  if (path == "/media/subscriptions")
    return handleSubscriptions(request, response);

  if (path == "/media/subscriptions/process" || path == "/media/providers/remote/sync"
      || path == "/updater/check")
  {
    writeContainer(request, response, { { "size", 0 } });
    return true;
  }

  if (path.startsWith("/media/subscriptions/"))
    return handleSubscription(request, response);

  if (path == "/activities" || path.startsWith("/activities/"))
    return handleActivities(request, response);

  return false;
}

/////////////////////////////////////////////////////////////////////////////////////////
const MediaSubscriptions::Provider* MediaSubscriptions::provider(const QString& source) const
{
  auto it = m_providers.constFind(source);
  return it == m_providers.constEnd() ? nullptr : &it.value();
}

/////////////////////////////////////////////////////////////////////////////////////////
MediaSubscriptions::Provider& MediaSubscriptions::providerFor(const QString& source)
{
  if (!m_providers.contains(source))
  {
    Provider created;
    created.id = m_nextProviderId++;
    created.source = source;
    splitSource(source, created.identifier, created.providerIdentifier);
    created.title = created.identifier;
    m_providers.insert(source, created);
  }

  return m_providers[source];
}

/////////////////////////////////////////////////////////////////////////////////////////
QString MediaSubscriptions::secureUrl(const QString& source) const
{
  const Provider* entry = provider(source);
  if (!entry)
    return QString();

  for (const QString& url : entry->urls)
    if (QUrl(url).scheme() == QLatin1String("https"))
      return url;

  return QString();
}

/////////////////////////////////////////////////////////////////////////////////////////
QVariantList MediaSubscriptions::providerNodes() const
{
  QVariantList nodes;
  nodes.append(node(QStringLiteral("MediaProvider"),
                    { { "identifier", "com.plexapp.plugins.library" },
                      { "title", "Library" },
                      { "types", "video,audio,photo" },
                      { "protocols", "stream" } }));

  for (const QString& source : m_providerOrder)
  {
    const Provider* entry = provider(source);
    if (!entry)
      continue;

    nodes.append(node(QStringLiteral("MediaProvider"),
                      { { "id", entry->id },
                        { "identifier", entry->identifier },
                        { "providerIdentifier", entry->providerIdentifier },
                        { "title", entry->title },
                        { "source", entry->source },
                        { "subscriptions", 1 } }));
  }

  return nodes;
}

/////////////////////////////////////////////////////////////////////////////////////////
bool MediaSubscriptions::handleProviders(const QHttpServerRequest& request, HttpResponse* response)
{
  if (request.method() == QHttpServerRequest::Method::Post)
  {
    const QUrlQuery query(request.url().query());
    const QString source = query.queryItemValue("url", QUrl::FullyDecoded);

    if (source.isEmpty())
    {
      response->setStatusCode(HttpResponse::StatusCode::BadRequest);
      response->addHeader("Access-Control-Allow-Origin", "*");
      response->end();
      return true;
    }

    Provider& entry = providerFor(source);
    if (!m_providerOrder.contains(source))
      m_providerOrder.append(source);
    save();

    QLOG_INFO() << "Registered download provider" << qPrintable(entry.identifier);

    writeContainer(request, response, { { "size", 1 } },
                   { node(QStringLiteral("MediaProvider"),
                          { { "id", entry.id },
                            { "identifier", entry.identifier },
                            { "providerIdentifier", entry.providerIdentifier },
                            { "title", entry.title },
                            { "source", entry.source },
                            { "subscriptions", 1 } }) });
    return true;
  }

  const QVariantList nodes = providerNodes();
  writeContainer(request, response,
                 { { "size", nodes.size() },
                   { "machineIdentifier", m_machineIdentifier },
                   { "version", QCoreApplication::applicationVersion() },
                   { "allowSync", 0 } },
                 nodes);
  return true;
}

/////////////////////////////////////////////////////////////////////////////////////////
bool MediaSubscriptions::handleConnection(const QHttpServerRequest& request, HttpResponse* response)
{
  const QString path = request.url().path();
  const QString encoded = path.mid(17, path.size() - 17 - 11);
  const QString source = QUrl::fromPercentEncoding(encoded.toUtf8());

  const QUrlQuery query(request.url().query());

  if (request.method() == QHttpServerRequest::Method::Delete)
  {
    if (m_providers.contains(source))
    {
      m_providers[source].urls.clear();
      m_providers[source].token.clear();
    }
  }
  else
  {
    const QString url = query.queryItemValue("url", QUrl::FullyDecoded);
    const QString token = query.queryItemValue("auth_token", QUrl::FullyDecoded);

    Provider& entry = providerFor(source);
    if (!m_providerOrder.contains(source))
      m_providerOrder.append(source);

    if (!token.isEmpty())
      entry.token = token;

    if (!url.isEmpty())
    {
      if (QUrl(url).scheme() == QLatin1String("https"))
      {
        if (!entry.urls.contains(url))
          entry.urls.prepend(url);
      }
      else
      {
        QLOG_WARN() << "Refusing a plaintext connection for downloads from"
                    << qPrintable(entry.identifier) << "- type"
                    << qPrintable(query.queryItemValue("connectionType"));
      }
    }

    save();
    pump();
  }

  writeContainer(request, response, { { "size", 0 } });
  return true;
}

/////////////////////////////////////////////////////////////////////////////////////////
bool MediaSubscriptions::handleTemplate(const QHttpServerRequest& request, HttpResponse* response)
{
  const QUrlQuery query(request.url().query());
  const QString source = query.queryItemValue("source", QUrl::FullyDecoded);
  const QString ratingKey = query.queryItemValue("ratingKey", QUrl::FullyDecoded);
  const QString guid = query.queryItemValue("guid", QUrl::FullyDecoded);

  QString parameters = QStringLiteral("source=%1&ratingKey=%2")
                         .arg(QString::fromUtf8(QUrl::toPercentEncoding(source)),
                              QString::fromUtf8(QUrl::toPercentEncoding(ratingKey)));
  if (!guid.isEmpty())
    parameters += QStringLiteral("&guid=%1").arg(QString::fromUtf8(QUrl::toPercentEncoding(guid)));

  int guidType = 0;
  const int scheme = guid.indexOf(QLatin1String("://"));
  if (scheme >= 0)
  {
    const QString rest = guid.mid(scheme + 3);
    guidType = typeId(rest.left(rest.indexOf('/')));
  }
  if (!guidType)
    guidType = g_types[0].id;

  QVariantList entries;
  QVariantList nodes;
  for (int pass = 0; pass < 2; pass++)
  {
    for (const TypeEntry& type : g_types)
    {
      if ((type.id == guidType) != (pass == 0))
        continue;

      QVariantMap attributes = { { "type", type.id },
                                 { "targetLibrarySectionID", "1" },
                                 { "targetSectionLocationID", "1" },
                                 { "parameters", parameters } };

      if (type.id == guidType)
        attributes.insert(QStringLiteral("selected"), 1);

      QVariantMap hint = { { "ratingKey", ratingKey },
                           { "source", source },
                           { "type", QString::fromLatin1(type.name) } };
      if (!guid.isEmpty())
        hint.insert(QStringLiteral("guid"), guid);

      QVariantMap hintChild = node(QStringLiteral("Metadata"), hint);
      hintChild.insert(SINGLE, 1);

      QVariantMap setting = node(QStringLiteral("Setting"),
                                 { { "id", "oneShot" }, { "value", "false" }, { "hidden", 1 } });

      entries.append(node(QStringLiteral("MediaSubscription"), attributes, { setting }));
      nodes.append(node(QStringLiteral("SubscriptionTemplate"), attributes, { hintChild }));
    }
  }

  QVariantMap first = nodes.takeFirst().toMap();
  QVariantList children = first.value(KIDS).toList();
  children.append(entries);
  first.insert(KIDS, children);
  nodes.prepend(first);

  writeContainer(request, response, { { "size", nodes.size() } }, nodes);
  return true;
}

/////////////////////////////////////////////////////////////////////////////////////////
QVariantMap MediaSubscriptions::hintNode(const Subscription& subscription) const
{
  QVariantMap hint = { { "ratingKey", subscription.ratingKey },
                       { "source", subscription.source },
                       { "type", subscription.typeString },
                       { "title", subscription.title } };

  if (!subscription.guid.isEmpty())
    hint.insert(QStringLiteral("guid"), subscription.guid);
  if (!subscription.thumb.isEmpty())
    hint.insert(QStringLiteral("thumb"), subscription.thumb);
  if (!subscription.parentTitle.isEmpty())
    hint.insert(QStringLiteral("parentTitle"), subscription.parentTitle);
  if (!subscription.grandparentTitle.isEmpty())
    hint.insert(QStringLiteral("grandparentTitle"), subscription.grandparentTitle);
  if (!subscription.year.isEmpty())
    hint.insert(QStringLiteral("year"), subscription.year);
  if (subscription.index)
    hint.insert(QStringLiteral("index"), subscription.index);
  if (subscription.parentIndex)
    hint.insert(QStringLiteral("parentIndex"), subscription.parentIndex);

  QVariantMap hintNodeValue = node(QStringLiteral("Metadata"), hint);
  hintNodeValue.insert(SINGLE, 1);
  return hintNodeValue;
}

/////////////////////////////////////////////////////////////////////////////////////////
QVariantMap MediaSubscriptions::subscriptionNode(const Subscription& subscription,
                                                 bool includeGrabs) const
{
  QVariantMap attributes = { { "key", subscription.key },
                             { "type", subscription.type },
                             { "title", subscription.title },
                             { "createdAt", subscription.createdAt },
                             { "storageTotal", subscription.storageTotal } };

  const Provider* entry = provider(subscription.source);
  if (entry)
    attributes.insert(QStringLiteral("provider"), entry->identifier);

  QVariantList children = { hintNode(subscription) };

  if (includeGrabs && !subscription.error.isEmpty())
  {
    const QString itemKey = QStringLiteral("/library/metadata/%1").arg(subscription.ratingKey);

    QVariantMap item = node(QStringLiteral("Metadata"), { { "key", itemKey },
                                                          { "ratingKey", subscription.ratingKey },
                                                          { "type", subscription.typeString },
                                                          { "title", subscription.title } });
    item.insert(SINGLE, 1);

    children.append(node(
      QStringLiteral("MediaGrabOperation"),
      { { "key", QStringLiteral("/media/grabbers/operations/%1").arg(subscription.key) },
        { "mediaSubscriptionID", subscription.key },
        { "mediaIndex", 0 },
        { "status", "error" },
        { "error", subscription.error } },
      { item }));
  }

  return node(QStringLiteral("MediaSubscription"), attributes, children);
}

/////////////////////////////////////////////////////////////////////////////////////////
bool MediaSubscriptions::handleSubscriptions(const QHttpServerRequest& request,
                                             HttpResponse* response)
{
  if (request.method() == QHttpServerRequest::Method::Post)
  {
    createSubscription(request, response);
    return true;
  }

  const QUrlQuery query(request.url().query());
  const bool includeGrabs = query.hasQueryItem("includeGrabs");

  QVariantList nodes;
  for (const QString& key : m_order)
  {
    auto it = m_subscriptions.constFind(key);
    if (it != m_subscriptions.constEnd())
      nodes.append(subscriptionNode(it.value(), includeGrabs));
  }

  writeContainer(request, response, { { "size", nodes.size() } }, nodes);
  return true;
}

/////////////////////////////////////////////////////////////////////////////////////////
void MediaSubscriptions::createSubscription(const QHttpServerRequest& request,
                                            HttpResponse* response)
{
  const QUrlQuery query(request.url().query());
  const QString source = query.queryItemValue("source", QUrl::FullyDecoded);
  const QString ratingKey = query.queryItemValue("ratingKey", QUrl::FullyDecoded);
  const QString guid = query.queryItemValue("guid", QUrl::FullyDecoded);
  const int type = query.queryItemValue("type").toInt();

  if (source.isEmpty() || ratingKey.isEmpty())
  {
    QLOG_WARN() << "Refusing a download with no source or rating key";
    response->setStatusCode(HttpResponse::StatusCode::BadRequest);
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->end();
    return;
  }

  const Provider* entry = provider(source);
  if (!entry || entry->token.isEmpty() || secureUrl(source).isEmpty())
  {
    QLOG_WARN() << "Refusing a download from" << qPrintable(source)
                << "- no verified https connection has been registered";
    response->setStatusCode(HttpResponse::StatusCode::ServiceUnavailable);
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->end();
    return;
  }

  Subscription subscription;
  subscription.key = QString::number(m_nextKey++);
  subscription.type = type;
  subscription.typeString = typeName(type);
  subscription.source = source;
  subscription.ratingKey = ratingKey;
  subscription.guid = guid;
  subscription.createdAt = QDateTime::currentSecsSinceEpoch();

  m_subscriptions.insert(subscription.key, subscription);
  if (!m_order.contains(subscription.key))
    m_order.append(subscription.key);

  QLOG_INFO() << "Created download subscription" << qPrintable(subscription.key) << "for"
              << qPrintable(ratingKey);

  fetchHint(subscription.key, QPointer<HttpResponse>(response), wantsJson(request));
}

/////////////////////////////////////////////////////////////////////////////////////////
void MediaSubscriptions::fetchHint(const QString& key, QPointer<HttpResponse> response,
                                   bool wantsJson)
{
  const Subscription subscription = m_subscriptions.value(key);
  const QString base = secureUrl(subscription.source);
  const Provider* entry = provider(subscription.source);

  QNetworkRequest metadata(QUrl(base + QStringLiteral("/library/metadata/%1").arg(subscription.ratingKey)));
  metadata.setRawHeader("Accept", "application/json");
  metadata.setRawHeader("X-Plex-Token", entry->token.toUtf8());

  QNetworkReply* reply = m_network->get(metadata);
  connect(reply, &QNetworkReply::finished, this, [this, key, response, wantsJson, reply]()
  {
    reply->deleteLater();

    if (!m_subscriptions.contains(key))
      return;

    Subscription& subscription = m_subscriptions[key];

    if (reply->error() == QNetworkReply::NoError)
    {
      applyHint(subscription, reply->readAll());
    }
    else
    {
      QLOG_WARN() << "Could not read the item to download:" << qPrintable(reply->errorString());
      subscription.error = tr("The server did not describe this item.");
    }

    save();

    if (response)
      writeContainer(wantsJson, response.data(), { { "size", 1 } },
                     { subscriptionNode(subscription, true) });

    if (subscription.error.isEmpty())
      enqueue(key);
  });
}

/////////////////////////////////////////////////////////////////////////////////////////
void MediaSubscriptions::applyHint(Subscription& subscription, const QByteArray& body)
{
  const QJsonObject container =
    QJsonDocument::fromJson(body).object().value("MediaContainer").toObject();
  const QJsonArray items = container.value("Metadata").toArray();

  if (items.isEmpty())
  {
    subscription.error = tr("The server did not describe this item.");
    return;
  }

  const QJsonObject item = items.at(0).toObject();

  subscription.title = item.value("title").toString();
  subscription.parentTitle = item.value("parentTitle").toString();
  subscription.grandparentTitle = item.value("grandparentTitle").toString();
  subscription.thumb = item.value("thumb").toString();
  subscription.year = item.value("year").isUndefined()
                        ? QString()
                        : QString::number(item.value("year").toInt());
  subscription.index = item.value("index").toInt();
  subscription.parentIndex = item.value("parentIndex").toInt();

  const QString reported = item.value("type").toString();
  if (!reported.isEmpty())
  {
    subscription.typeString = reported;
    if (const int id = typeId(reported))
      subscription.type = id;
  }

  const QJsonArray media = item.value("Media").toArray();
  if (!media.isEmpty())
  {
    const QJsonArray parts = media.at(0).toObject().value("Part").toArray();
    if (!parts.isEmpty())
    {
      const QJsonObject part = parts.at(0).toObject();
      subscription.partKey = part.value("key").toString();
      subscription.total = part.value("size").toVariant().toLongLong();

      QString name = QFileInfo(part.value("file").toString()).fileName();
      if (name.isEmpty())
      {
        name = subscription.ratingKey;
        const QString extension = part.value("container").toString();
        if (!extension.isEmpty())
          name += "." + extension;
      }

      QString machine, identifier;
      splitSource(subscription.source, machine, identifier);

      const QString directory =
        Paths::dataDir(QStringLiteral("downloads/%1/%2").arg(machine, subscription.ratingKey));
      QDir().mkpath(directory);
      subscription.filePath = directory + "/" + name;
    }
  }

  if (subscription.partKey.isEmpty())
    subscription.error = tr("This item has no downloadable file.");
}

/////////////////////////////////////////////////////////////////////////////////////////
bool MediaSubscriptions::handleSubscription(const QHttpServerRequest& request,
                                            HttpResponse* response)
{
  const QString key = request.url().path().mid(21);

  if (!m_subscriptions.contains(key))
  {
    response->setStatusCode(HttpResponse::StatusCode::NotFound);
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->end();
    return true;
  }

  if (request.method() == QHttpServerRequest::Method::Delete)
  {
    const Subscription subscription = m_subscriptions.value(key);

    if (m_current == key && m_grab)
      m_grab->abort();

    m_queue.removeAll(key);
    m_order.removeAll(key);
    m_subscriptions.remove(key);

    if (!subscription.filePath.isEmpty())
    {
      QFile::remove(subscription.filePath);
      QFile::remove(subscription.filePath + ".part");
    }

    save();

    QLOG_INFO() << "Deleted download subscription" << qPrintable(key);
    writeContainer(request, response, { { "size", 0 } });
    return true;
  }

  Subscription& subscription = m_subscriptions[key];
  subscription.error.clear();
  subscription.complete = false;
  subscription.received = 0;
  save();
  enqueue(key);

  writeContainer(request, response, { { "size", 1 } },
                 { subscriptionNode(subscription, true) });
  return true;
}

/////////////////////////////////////////////////////////////////////////////////////////
QVariantMap MediaSubscriptions::activityNode(const Subscription& subscription,
                                             const QString& type) const
{
  const bool refresh = type == QLatin1String(ACTIVITY_REFRESH);
  const bool running = subscription.active && subscription.total > 0;

  QVariantMap attributes = { { "uuid", refresh ? subscription.refreshUuid : subscription.grabUuid },
                             { "type", type },
                             { "cancellable", 1 },
                             { "title", subscription.title } };

  QVariantMap context = { { "subscriptionID", subscription.key },
                          { "itemKey", QStringLiteral("/library/metadata/%1").arg(subscription.ratingKey) },
                          { "ratingKey", subscription.ratingKey },
                          { "source", subscription.source } };

  if (running)
  {
    int progress = 0;
    if (subscription.total > 0)
      progress = int((subscription.received * 100) / subscription.total);

    attributes.insert(QStringLiteral("progress"), qBound(0, progress, 100));
    context.insert(QStringLiteral("total"), 1);
    context.insert(QStringLiteral("completed"), 0);
  }

  QVariantMap contextNode = node(QStringLiteral("Context"), context);
  contextNode.insert(SINGLE, 1);

  return node(QStringLiteral("Activity"), attributes, { contextNode });
}

/////////////////////////////////////////////////////////////////////////////////////////
void MediaSubscriptions::publish(const Subscription& subscription, const QString& type,
                                 const QString& event)
{
  QVariantMap activity = activityNode(subscription, type);

  if (event == QLatin1String("ended"))
  {
    QVariantMap status = node(QStringLiteral("Response"),
                              { { "status", subscription.error.isEmpty() ? "complete" : "error" } });
    status.insert(SINGLE, 1);

    QVariantList children = activity.value(KIDS).toList();
    children.append(status);
    activity.insert(KIDS, children);
  }

  QJsonObject item;
  item.insert(QStringLiteral("event"), event);
  item.insert(QStringLiteral("uuid"), activity.value(QStringLiteral("uuid")).toString());
  item.insert(QStringLiteral("Activity"), nodeToJson(activity));

  QJsonObject container;
  container.insert(QStringLiteral("type"), QStringLiteral("activity"));
  container.insert(QStringLiteral("size"), 1);
  container.insert(QStringLiteral("ActivityNotification"), QJsonArray({ item }));

  QJsonObject root;
  root.insert(QStringLiteral("NotificationContainer"), container);

  emit notification(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

/////////////////////////////////////////////////////////////////////////////////////////
bool MediaSubscriptions::handleActivities(const QHttpServerRequest& request, HttpResponse* response)
{
  const QString path = request.url().path();

  if (path != "/activities")
  {
    const QString uuid = path.mid(12);

    for (const QString& key : m_order)
    {
      if (!m_subscriptions.contains(key))
        continue;

      const Subscription& subscription = m_subscriptions.value(key);
      if (subscription.refreshUuid != uuid && subscription.grabUuid != uuid)
        continue;

      QLOG_INFO() << "Cancelling download" << qPrintable(key);

      if (!m_cancelled.contains(key))
        m_cancelled.append(key);

      if (m_current == key && m_grab)
      {
        m_grab->abort();
      }
      else
      {
        m_queue.removeAll(key);
        finishGrab(key, QStringLiteral("cancelled"));
      }

      break;
    }

    writeContainer(request, response, { { "size", 0 } });
    return true;
  }

  QVariantList nodes;
  for (const QString& key : m_order)
  {
    auto it = m_subscriptions.constFind(key);
    if (it == m_subscriptions.constEnd())
      continue;

    const Subscription& subscription = it.value();
    if (subscription.refreshUuid.isEmpty())
      continue;

    nodes.append(activityNode(subscription, QStringLiteral(ACTIVITY_REFRESH)));
    nodes.append(activityNode(subscription, QStringLiteral(ACTIVITY_GRAB)));
  }

  writeContainer(request, response, { { "size", nodes.size() } }, nodes);
  return true;
}

/////////////////////////////////////////////////////////////////////////////////////////
bool MediaSubscriptions::handleMapping(const QHttpServerRequest& request, HttpResponse* response)
{
  const QString path = request.url().path();
  const QString keys = path.mid(path.lastIndexOf('/') + 1);

  QVariantList nodes;
  for (const QString& ratingKey : keys.split(',', Qt::SkipEmptyParts))
  {
    for (const QString& key : m_order)
    {
      auto it = m_subscriptions.constFind(key);
      if (it == m_subscriptions.constEnd() || it.value().ratingKey != ratingKey
          || !it.value().complete)
        continue;

      nodes.append(node(QStringLiteral("MediaSubscription"),
                        { { "id", it.value().key },
                          { "itemRatingKey", ratingKey },
                          { "type", it.value().type } }));
      break;
    }
  }

  writeContainer(request, response, { { "size", nodes.size() } }, nodes);
  return true;
}

/////////////////////////////////////////////////////////////////////////////////////////
void MediaSubscriptions::enqueue(const QString& key)
{
  if (!m_subscriptions.contains(key) || m_queue.contains(key) || m_current == key)
    return;

  Subscription& subscription = m_subscriptions[key];
  subscription.refreshUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
  subscription.grabUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
  subscription.active = false;

  m_queue.append(key);

  publish(subscription, QStringLiteral(ACTIVITY_REFRESH), QStringLiteral("started"));
  pump();
}

/////////////////////////////////////////////////////////////////////////////////////////
void MediaSubscriptions::pump()
{
  if (!m_current.isEmpty() || m_queue.isEmpty())
    return;

  const QString key = m_queue.takeFirst();
  if (!m_subscriptions.contains(key))
  {
    pump();
    return;
  }

  beginGrab(key);
}

/////////////////////////////////////////////////////////////////////////////////////////
void MediaSubscriptions::beginGrab(const QString& key)
{
  Subscription& subscription = m_subscriptions[key];

  const QString base = secureUrl(subscription.source);
  const Provider* entry = provider(subscription.source);

  if (base.isEmpty() || !entry || entry->token.isEmpty())
  {
    QLOG_INFO() << "Holding download" << qPrintable(key)
                << "until a verified https connection is registered";
    m_queue.append(key);
    return;
  }

  if (subscription.partKey.isEmpty())
  {
    finishGrab(key, tr("This item has no downloadable file."));
    return;
  }

  const QUrl url(base + subscription.partKey);
  if (url.scheme() != QLatin1String("https"))
  {
    QLOG_WARN() << "Refusing to download over" << qPrintable(url.scheme());
    finishGrab(key, tr("Downloads need a secure connection to this server."));
    return;
  }

  m_sink = new QFile(subscription.filePath + ".part", this);
  if (!m_sink->open(QIODevice::WriteOnly | QIODevice::Truncate))
  {
    const QString reason = m_sink->errorString();
    delete m_sink;
    m_sink = nullptr;
    finishGrab(key, reason);
    return;
  }

  m_current = key;
  subscription.active = true;
  subscription.received = 0;

  QNetworkRequest grab(url);
  grab.setRawHeader("X-Plex-Token", entry->token.toUtf8());
  grab.setAttribute(QNetworkRequest::HttpPipeliningAllowedAttribute, true);

  QLOG_INFO() << "Downloading" << qPrintable(subscription.ratingKey) << "from"
              << qPrintable(url.host());

  m_grab = m_network->get(grab);

  connect(m_grab, &QNetworkReply::readyRead, this, [this, key]()
  {
    if (m_sink && m_grab)
      m_sink->write(m_grab->readAll());
  });

  connect(m_grab, &QNetworkReply::downloadProgress, this, [this, key](qint64 received, qint64 total)
  {
    if (!m_subscriptions.contains(key))
      return;

    Subscription& subscription = m_subscriptions[key];
    subscription.received = received;
    if (total > 0)
      subscription.total = total;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastPublish < 500)
      return;

    m_lastPublish = now;
    publish(subscription, QStringLiteral(ACTIVITY_REFRESH), QStringLiteral("updated"));
  });

  connect(m_grab, &QNetworkReply::sslErrors, this, [](const QList<QSslError>& errors)
  {
    for (const QSslError& error : errors)
      QLOG_WARN() << "Refusing the download connection:" << qPrintable(error.errorString());
  });

  connect(m_grab, &QNetworkReply::finished, this, [this, key]()
  {
    QString error;
    if (m_grab && m_grab->error() != QNetworkReply::NoError)
      error = m_grab->errorString();

    if (m_sink && m_grab)
      m_sink->write(m_grab->readAll());

    if (m_grab)
      m_grab->deleteLater();

    finishGrab(key, error);
  });

  publish(subscription, QStringLiteral(ACTIVITY_GRAB), QStringLiteral("started"));
  publish(subscription, QStringLiteral(ACTIVITY_REFRESH), QStringLiteral("updated"));
}

/////////////////////////////////////////////////////////////////////////////////////////
void MediaSubscriptions::finishGrab(const QString& key, const QString& error)
{
  if (m_sink)
  {
    m_sink->close();
    delete m_sink;
    m_sink = nullptr;
  }

  m_grab = nullptr;
  if (m_current == key)
    m_current.clear();

  if (m_cancelled.removeAll(key) && m_subscriptions.contains(key))
  {
    const Subscription subscription = m_subscriptions.value(key);

    m_queue.removeAll(key);
    m_order.removeAll(key);
    m_subscriptions.remove(key);

    if (!subscription.filePath.isEmpty())
    {
      QFile::remove(subscription.filePath);
      QFile::remove(subscription.filePath + ".part");
    }

    save();

    Subscription ended = subscription;
    ended.active = false;
    ended.error = QStringLiteral("cancelled");
    publish(ended, QStringLiteral(ACTIVITY_REFRESH), QStringLiteral("ended"));
    publish(ended, QStringLiteral(ACTIVITY_GRAB), QStringLiteral("ended"));

    pump();
    return;
  }

  if (m_subscriptions.contains(key))
  {
    Subscription& subscription = m_subscriptions[key];
    subscription.active = false;
    subscription.error = error;

    if (error.isEmpty())
    {
      const QString part = subscription.filePath + ".part";
      QFile::remove(subscription.filePath);

      if (QFile::rename(part, subscription.filePath))
      {
        subscription.complete = true;
        subscription.storageTotal = QFileInfo(subscription.filePath).size();
        QLOG_INFO() << "Downloaded" << qPrintable(subscription.ratingKey)
                    << subscription.storageTotal << "bytes";
      }
      else
      {
        subscription.error = tr("The download could not be saved.");
      }
    }
    else
    {
      QFile::remove(subscription.filePath + ".part");
      QLOG_WARN() << "Download" << qPrintable(key) << "failed:" << qPrintable(error);
    }

    save();

    publish(subscription, QStringLiteral(ACTIVITY_REFRESH), QStringLiteral("ended"));
    publish(subscription, QStringLiteral(ACTIVITY_GRAB), QStringLiteral("ended"));

    subscription.refreshUuid.clear();
    subscription.grabUuid.clear();
  }

  pump();
}

/////////////////////////////////////////////////////////////////////////////////////////
void MediaSubscriptions::load()
{
  QFile file(Paths::dataDir("downloads/subscriptions.json"));
  if (!file.open(QIODevice::ReadOnly))
    return;

  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();

  for (const QJsonValue value : root.value("providers").toArray())
  {
    const QJsonObject object = value.toObject();

    Provider entry;
    entry.id = object.value("id").toInt();
    entry.source = object.value("source").toString();
    entry.identifier = object.value("identifier").toString();
    entry.providerIdentifier = object.value("providerIdentifier").toString();
    entry.title = object.value("title").toString();

    if (entry.source.isEmpty())
      continue;

    m_nextProviderId = qMax(m_nextProviderId, entry.id + 1);
    m_providers.insert(entry.source, entry);
    m_providerOrder.append(entry.source);
  }

  for (const QJsonValue value : root.value("subscriptions").toArray())
  {
    const QJsonObject object = value.toObject();

    Subscription subscription;
    subscription.key = object.value("key").toString();
    subscription.type = object.value("type").toInt();
    subscription.typeString = object.value("typeString").toString();
    subscription.source = object.value("source").toString();
    subscription.ratingKey = object.value("ratingKey").toString();
    subscription.guid = object.value("guid").toString();
    subscription.title = object.value("title").toString();
    subscription.parentTitle = object.value("parentTitle").toString();
    subscription.grandparentTitle = object.value("grandparentTitle").toString();
    subscription.thumb = object.value("thumb").toString();
    subscription.year = object.value("year").toString();
    subscription.index = object.value("index").toInt();
    subscription.parentIndex = object.value("parentIndex").toInt();
    subscription.createdAt = object.value("createdAt").toVariant().toLongLong();
    subscription.storageTotal = object.value("storageTotal").toVariant().toLongLong();
    subscription.partKey = object.value("partKey").toString();
    subscription.filePath = object.value("filePath").toString();
    subscription.error = object.value("error").toString();
    subscription.complete = object.value("complete").toBool();

    if (subscription.key.isEmpty())
      continue;

    m_nextKey = qMax(m_nextKey, subscription.key.toInt() + 1);
    m_subscriptions.insert(subscription.key, subscription);
    m_order.append(subscription.key);

    if (!subscription.complete && subscription.error.isEmpty())
      m_queue.append(subscription.key);
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
void MediaSubscriptions::save() const
{
  QJsonArray providers;
  for (auto it = m_providers.constBegin(); it != m_providers.constEnd(); ++it)
  {
    QJsonObject object;
    object.insert("id", it.value().id);
    object.insert("source", it.value().source);
    object.insert("identifier", it.value().identifier);
    object.insert("providerIdentifier", it.value().providerIdentifier);
    object.insert("title", it.value().title);
    providers.append(object);
  }

  QJsonArray subscriptions;
  for (auto it = m_subscriptions.constBegin(); it != m_subscriptions.constEnd(); ++it)
  {
    const Subscription& subscription = it.value();

    QJsonObject object;
    object.insert("key", subscription.key);
    object.insert("type", subscription.type);
    object.insert("typeString", subscription.typeString);
    object.insert("source", subscription.source);
    object.insert("ratingKey", subscription.ratingKey);
    object.insert("guid", subscription.guid);
    object.insert("title", subscription.title);
    object.insert("parentTitle", subscription.parentTitle);
    object.insert("grandparentTitle", subscription.grandparentTitle);
    object.insert("thumb", subscription.thumb);
    object.insert("year", subscription.year);
    object.insert("index", subscription.index);
    object.insert("parentIndex", subscription.parentIndex);
    object.insert("createdAt", subscription.createdAt);
    object.insert("storageTotal", subscription.storageTotal);
    object.insert("partKey", subscription.partKey);
    object.insert("filePath", subscription.filePath);
    object.insert("error", subscription.error);
    object.insert("complete", subscription.complete);
    subscriptions.append(object);
  }

  QJsonObject root;
  root.insert("providers", providers);
  root.insert("subscriptions", subscriptions);

  const QString path = Paths::dataDir("downloads/subscriptions.json");
  QDir().mkpath(QFileInfo(path).absolutePath());

  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly))
  {
    QLOG_WARN() << "Could not store download subscriptions:" << qPrintable(file.errorString());
    return;
  }

  file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
  file.commit();
}
