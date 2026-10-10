#include "ConnectorBase.h"

#include "QsLog.h"

namespace Envelope
{
  QVariantMap ok(const QVariant& result)
  {
    QVariantMap m;
    m["errorCode"] = Ok;
    m["result"] = result;
    return m;
  }

  QVariantMap failed(int code)
  {
    QVariantMap m;
    m["errorCode"] = code;
    return m;
  }

  QVariantMap deferred(int index)
  {
    QVariantMap inner;
    inner["index"] = index;
    QVariantMap payload;
    payload["deferred"] = inner;

    QVariantMap m;
    m["errorCode"] = Deferred;
    m["result"] = payload;
    return m;
  }
}

ConnectorBase::ConnectorBase(const QString& name, QObject* parent)
  : QObject(parent), m_name(name)
{
}

bool ConnectorBase::readKey(const QString& key, QVariant& out) const
{
  if (!m_data.contains(key))
    return false;

  out = m_data.value(key);
  return true;
}

bool ConnectorBase::writeKey(const QString& key, const QVariant& value)
{
  m_data[key] = value;
  return true;
}

QStringList ConnectorBase::readableKeys() const
{
  return m_data.keys();
}

void ConnectorBase::trace(const QString& call, const QVariant& detail) const
{
  QLOG_DEBUG() << qPrintable(m_name + "." + call) << detail;
}

QVariantMap ConnectorBase::getAll()
{
  QVariantMap per;
  for (const QString& key : readableKeys())
  {
    QVariant value;
    per[key] = readKey(key, value) ? Envelope::ok(value) : Envelope::failed();
  }
  trace("getAll", QVariant(per.keys()));
  return Envelope::ok(per);
}

QVariantMap ConnectorBase::get(const QVariantList& keys)
{
  QVariantMap per;
  QStringList missing;
  for (const QVariant& k : keys)
  {
    const QString key = k.toString();
    QVariant value;
    if (readKey(key, value))
    {
      per[key] = Envelope::ok(value);
    }
    else
    {
      per[key] = Envelope::failed();
      missing << key;
    }
  }
  trace("get", missing.isEmpty() ? QVariant(keys)
                                 : QVariant("unknown keys: " + missing.join(", ")));
  return Envelope::ok(per);
}

QVariantMap ConnectorBase::set(const QVariantMap& values)
{
  QVariantMap per;
  for (auto it = values.constBegin(); it != values.constEnd(); ++it)
  {
    if (writeKey(it.key(), it.value()))
    {
      per[it.key()] = Envelope::ok();
      emit onChanged(it.key(), it.value());
    }
    else
    {
      QLOG_DEBUG() << m_name << "rejected set of" << it.key();
      per[it.key()] = Envelope::failed();
    }
  }
  trace("set", QVariant(values.keys()));
  return Envelope::ok(per);
}

QVariantMap ConnectorBase::keys()
{
  const QStringList k = readableKeys();
  trace("keys", QVariant(k.size()));
  return Envelope::ok(QVariant(k));
}
