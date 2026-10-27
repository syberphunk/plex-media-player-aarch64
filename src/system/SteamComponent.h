#ifndef STEAMCOMPONENT_H
#define STEAMCOMPONENT_H

#include <QString>
#include <QStringList>

#include "ComponentManager.h"
#include "utils/Utils.h"

class SteamComponent : public ComponentBase
{
  Q_OBJECT
  DEFINE_SINGLETON(SteamComponent);

public:
  const char* componentName() override { return "steam"; }
  bool componentExport() override { return false; }
  bool componentInitialize() override;
  void componentPostInitialize() override;

  Q_PROPERTY(bool launchedFromSteam READ launchedFromSteam CONSTANT)
  Q_PROPERTY(bool steamPresent READ steamPresent NOTIFY shortcutChanged)
  Q_PROPERTY(bool shortcutPresent READ shortcutPresent NOTIFY shortcutChanged)
  Q_PROPERTY(bool canRelaunch READ canRelaunch NOTIFY shortcutChanged)
  Q_PROPERTY(QString shortcutName READ shortcutName NOTIFY shortcutChanged)
  Q_PROPERTY(QString lastError READ lastError NOTIFY shortcutChanged)
  Q_PROPERTY(bool shouldOffer READ shouldOffer NOTIFY shortcutChanged)
  Q_PROPERTY(bool shouldOfferRelaunch READ shouldOfferRelaunch NOTIFY shortcutChanged)

  bool launchedFromSteam() const { return m_launchedFromSteam; }
  bool steamPresent() const { return !m_shortcutsPath.isEmpty(); }
  bool shortcutPresent() const { return m_shortcutIndex >= 0; }
  bool canRelaunch() const { return shortcutPresent() && m_runGameId != 0; }
  QString shortcutName() const { return m_shortcutName; }
  QString lastError() const { return m_lastError; }

  // True when the app is worth offering a Steam shortcut for: a headset
  // session that Steam did not start, where the shortcut would change how the
  // device clocks itself.
  bool shouldOffer() const;

  // The shortcut is already there but Steam did not start this session, so the
  // same session can be had with the device's per-title profile by starting it
  // through Steam instead.
  bool shouldOfferRelaunch() const;

  Q_INVOKABLE void refresh();
  Q_INVOKABLE bool createShortcut();
  Q_INVOKABLE bool relaunchThroughSteam();
  Q_INVOKABLE QString report() const;

  Q_SIGNAL void shortcutChanged();

private:
  explicit SteamComponent(QObject* parent = nullptr);

  struct Entry
  {
    int index = -1;
    QString appName;
    QString exe;
    QString launchOptions;
    quint32 appId = 0;
  };

  static QString findSteamRoot();
  static QString findShortcutsFile(const QString& steamRoot);
  static QList<Entry> parseShortcuts(const QByteArray& data, bool* ok);
  static QByteArray serializeEntry(int index, const Entry& entry);
  static quint32 shortcutAppId(const QString& exe, const QString& appName);
  static quint64 runGameId(quint32 appId);
  static QString ourFlatpakId();
  static QString exportedIconPath(const QString& flatpakId);

  bool backup(const QByteArray& data) const;

  bool m_launchedFromSteam = false;
  QString m_steamRoot;
  QString m_shortcutsPath;
  int m_shortcutIndex = -1;
  int m_entryCount = 0;
  QString m_shortcutName;
  QString m_lastError;
  quint64 m_runGameId = 0;
};

#endif
