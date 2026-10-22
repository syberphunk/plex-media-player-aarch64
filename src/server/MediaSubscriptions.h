#ifndef MEDIASUBSCRIPTIONS_H
#define MEDIASUBSCRIPTIONS_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariant>

#include "HttpResponse.h"

class QFile;
class QNetworkAccessManager;
class QNetworkReply;
class QHttpServerRequest;

class MediaSubscriptions : public QObject
{
  Q_OBJECT

public:
  explicit MediaSubscriptions(const QString& machineIdentifier, QObject* parent = nullptr);

  bool claims(const QString& path) const;
  bool handle(const QHttpServerRequest& request, HttpResponse* response);

  QVariantList providerNodes(bool includeStorage, bool wantsJson) const;

  static QVariantMap node(const QString& name, const QVariantMap& attributes,
                          const QVariantList& children = QVariantList());
  static void writeContainer(const QHttpServerRequest& request, HttpResponse* response,
                             const QVariantMap& attributes,
                             const QVariantList& children = QVariantList());
  static void writeContainer(bool wantsJson, HttpResponse* response,
                             const QVariantMap& attributes,
                             const QVariantList& children = QVariantList());

signals:
  void notification(const QByteArray& payload);

private:
  struct Connection
  {
    QString url;
    QString token;
  };

  struct Provider
  {
    int id = 0;
    QString source;
    QString identifier;
    QString providerIdentifier;
    QString title;
    QList<Connection> connections;
  };

  struct Subscription
  {
    QString key;
    int type = 0;
    QString typeString;
    QString source;
    QString ratingKey;
    QString guid;
    QString title;
    QString parentTitle;
    QString grandparentTitle;
    QString thumb;
    QString parentThumb;
    QString grandparentThumb;
    QString art;
    QString year;
    int index = 0;
    int parentIndex = 0;
    qint64 createdAt = 0;
    qint64 storageTotal = 0;
    QString partKey;
    QString filePath;
    QString error;
    QString refreshUuid;
    QString grabUuid;
    qint64 total = 0;
    qint64 received = 0;
    bool active = false;
    bool complete = false;
  };

  bool handleConnection(const QHttpServerRequest& request, HttpResponse* response);
  bool handleProviders(const QHttpServerRequest& request, HttpResponse* response);
  bool handleTemplate(const QHttpServerRequest& request, HttpResponse* response);
  bool handleSubscriptions(const QHttpServerRequest& request, HttpResponse* response);
  bool handleSubscription(const QHttpServerRequest& request, HttpResponse* response);
  bool handleActivities(const QHttpServerRequest& request, HttpResponse* response);
  bool handleMapping(const QHttpServerRequest& request, HttpResponse* response);
  bool handlePhoto(const QHttpServerRequest& request, HttpResponse* response);

  void createSubscription(const QHttpServerRequest& request, HttpResponse* response);
  void fetchHint(const QString& key, QPointer<HttpResponse> response, bool wantsJson);
  void applyHint(Subscription& subscription, const QByteArray& body);

  void enqueue(const QString& key);
  void pump();
  void beginGrab(const QString& key);
  void finishGrab(const QString& key, const QString& error);

  QVariantMap subscriptionNode(const Subscription& subscription, bool includeGrabs,
                               bool includeTarget) const;
  QVariantMap hintNode(const Subscription& subscription) const;
  QVariantMap targetNode(const Subscription& subscription) const;
  QVariantMap activityNode(const Subscription& subscription, const QString& type) const;
  void publish(const Subscription& subscription, const QString& type, const QString& event);

  const Provider* provider(const QString& source) const;
  const Subscription* subscriptionForImage(const QString& path) const;
  Provider& providerFor(const QString& source);
  bool secureConnection(const QString& source, Connection& connection) const;

  void load();
  void save() const;

  QString m_machineIdentifier;
  QHash<QString, Provider> m_providers;
  QStringList m_providerOrder;
  QHash<QString, Subscription> m_subscriptions;
  QStringList m_order;
  QStringList m_queue;
  QStringList m_cancelled;
  QString m_current;
  int m_nextKey = 1;
  int m_nextProviderId = 1;
  int m_nextOperation = 1;
  qint64 m_lastPublish = 0;

  QNetworkAccessManager* m_network;
  QPointer<QNetworkReply> m_grab;
  QFile* m_sink = nullptr;
};

#endif // MEDIASUBSCRIPTIONS_H
