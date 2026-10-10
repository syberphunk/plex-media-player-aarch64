#ifndef __CONNECTOR_BASE_H__
#define __CONNECTOR_BASE_H__

#include <QObject>
#include <QStringList>
#include <QVariantMap>

namespace Envelope
{
  static const int Ok = 0;
  static const int Failed = 1;
  static const int Deferred = 3;

  QVariantMap ok(const QVariant& result = QVariant());
  QVariantMap failed(int code = Failed);

  QVariantMap deferred(int index);
}

class ConnectorBase : public QObject
{
  Q_OBJECT

public:
  explicit ConnectorBase(const QString& name, QObject* parent = nullptr);

  const QString& connectorName() const { return m_name; }

  Q_INVOKABLE QVariantMap getAll();
  Q_INVOKABLE QVariantMap get(const QVariantList& keys);
  Q_INVOKABLE QVariantMap set(const QVariantMap& values);
  Q_INVOKABLE QVariantMap keys();

signals:
  void onChanged(const QString& key, const QVariant& value);

protected:
  virtual bool readKey(const QString& key, QVariant& out) const;
  virtual bool writeKey(const QString& key, const QVariant& value);
  virtual QStringList readableKeys() const;

  void trace(const QString& call, const QVariant& detail = QVariant()) const;

  QVariantMap m_data;
  QString m_name;
};

#endif
