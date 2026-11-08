#ifndef CAPABILITYCOMPONENT_H
#define CAPABILITYCOMPONENT_H

#include <QSize>
#include <QString>
#include <QStringList>

#include "ComponentManager.h"
#include "utils/Utils.h"

namespace Capability
{
  namespace Minimum
  {
    const int kWindowWide = 1920;
    const int kWindowTall = 1080;

    const int kStereoEyeWide = 960;
    const int kStereoEyeTall = 1080;

    const int kDecodeBitDepth = 8;

    const char kSteamOsVersion[] = "0.5.6";
  }

  enum class Weight
  {
    Note = 0,
    Warning = 1
  };

  struct Finding
  {
    QString id;
    QString text;
    QString remedy;
    Weight weight = Weight::Note;
  };
}

class CapabilityComponent : public ComponentBase
{
  Q_OBJECT
  DEFINE_SINGLETON(CapabilityComponent);

public:
  const char* componentName() override { return "capability"; }
  bool componentExport() override { return false; }
  bool componentInitialize() override;

  Q_PROPERTY(QStringList warnings READ warnings NOTIFY surveyed)
  Q_PROPERTY(QStringList remedies READ remedies NOTIFY surveyed)
  Q_PROPERTY(bool belowMinimum READ belowMinimum NOTIFY surveyed)
  Q_PROPERTY(bool dismissed READ dismissed WRITE setDismissed NOTIFY surveyed)
  Q_PROPERTY(QString environment READ environment NOTIFY surveyed)

  QStringList warnings() const;
  QStringList remedies() const;
  bool belowMinimum() const;
  bool dismissed() const { return m_dismissed; }
  void setDismissed(bool value);
  QString environment() const { return m_environment; }

  void survey(const QSize& window, const QString& codec, const QString& pixelFormat,
              int videoBitDepth, bool stereoWanted);

  Q_INVOKABLE void resurvey();

  Q_INVOKABLE QString report() const;

  Q_SIGNAL void surveyed();

private:
  explicit CapabilityComponent(QObject* parent = nullptr);

  QList<Capability::Finding> m_findings;
  QString m_environment;
  QString m_lastKey;
  QSize m_lastWindow;
  QString m_lastCodec;
  QString m_lastFormat;
  int m_lastDepth = 0;
  bool m_lastStereo = false;
  bool m_surveyed = false;
  bool m_dismissed = false;
};

#endif
