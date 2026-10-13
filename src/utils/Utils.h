#ifndef UTILS_H
#define UTILS_H

#include <QString>
#include <QUrl>
#include <QByteArray>
#include <QVariant>
#include <QException>
#include <QtCore/qjsondocument.h>

#define DEFINE_SINGLETON(cls) \
  public:                     \
  static cls& Get()           \
  {                           \
    static cls __instance;    \
    return __instance;        \
  }                           \


class FatalException : public QException
{
public:
  explicit FatalException(const QString& message) : m_message(message) {}
  const QString& message() const { return m_message; }

  ~FatalException() throw() override { }

private:
  QString m_message;
};

  enum Platform
  {
    PLATFORM_UNKNOWN = 0,
    PLATFORM_LINUX = (1 << 1),
    PLATFORM_ANY = PLATFORM_LINUX
  };

namespace Utils
{
  Platform CurrentPlatform();
  QJsonDocument OpenJsonDocument(const QString& path, QJsonParseError* err);
  QString CurrentUserId();
  QString ComputerName();
  QString PrimaryIPv4Address();
  QString ClientUUID();
  bool safelyWriteFile(const QString& filename, const QByteArray& data);
  QString sanitizeForHttpSeparators(const QString& input);
}

#endif // UTILS_H
