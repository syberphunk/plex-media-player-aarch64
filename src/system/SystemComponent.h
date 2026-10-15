#ifndef __SYSTEM_COMPONENT_H__
#define __SYSTEM_COMPONENT_H__

#include "ComponentManager.h"
#include <QTimer>
#include "utils/Utils.h"
#include "Paths.h"
#include "Names.h"

class SystemComponent : public ComponentBase
{
  Q_OBJECT
  DEFINE_SINGLETON(SystemComponent);

public:
  Q_PROPERTY(bool isLinux READ platformIsLinux CONSTANT)
  Q_PROPERTY(qreal scale MEMBER m_scale CONSTANT)

  bool componentExport() override { return true; }
  const char* componentName() override { return "system"; }
  bool componentInitialize() override;
  void componentPostInitialize() override;

  Q_INVOKABLE QVariantMap systemInformation() const;
  Q_INVOKABLE void exit();
  Q_INVOKABLE static void restart();

  Q_INVOKABLE void info(QString text);

  Q_INVOKABLE void setCursorVisibility(bool visible);

  Q_INVOKABLE QString getUserAgent();

  Q_INVOKABLE QString debugInformation();

  Q_INVOKABLE QStringList networkAddresses() const;
  Q_INVOKABLE int networkPort() const;

  Q_INVOKABLE void userInformation(const QVariantMap& userModel);

  Q_INVOKABLE void openExternalUrl(const QString& url);

  // called by the web-client when everything is properly inited
  Q_INVOKABLE void hello(const QString& version);

  Q_INVOKABLE QString getCapabilitiesString();
  Q_SIGNAL void capabilitiesChanged(const QString& capabilities);
  Q_SIGNAL void userInfoChanged();

  // possible os types type enum
  enum PlatformType
  {
    platformTypeUnknown,
    platformTypeLinux
  };

  // possible values for target types
  enum PlatformArch
  {
    platformArchUnknown,
    platformArchX86_32,
    platformArchX86_64,
    platformArchAarch64
  };

  inline PlatformType getPlatformType() { return m_platformType; }
  inline PlatformArch getPlatformArch() { return m_platformArch; }

  QString getPlatformTypeString() const;
  QString getPlatformArchString() const;

  bool isWebClientConnected() const { return !m_webClientVersion.isEmpty(); }

  inline QString authenticationToken() { return m_authenticationToken; }
  void setAuthenticationToken(const QString& token);
  inline bool cursorVisible() { return m_cursorVisible; }

  void updateScale(qreal scale);

signals:
  void hostMessage(const QString& message);
  void settingsMessage(const QString& setting, const QString& value);
  void scaleChanged(qreal scale);

private slots:
  void runUserScript(QString script);

private:
  explicit SystemComponent(QObject* parent = nullptr);
  static QMap<QString, QString> networkInterfaces();

  bool platformIsLinux() const { return m_platformType == platformTypeLinux; }

  QTimer* m_mouseOutTimer;
  PlatformType m_platformType;
  PlatformArch m_platformArch;
  bool m_doLogMessages;
  QString m_authenticationToken;
  QString m_webClientVersion;
  bool m_cursorVisible;
  qreal m_scale;

};

#endif
