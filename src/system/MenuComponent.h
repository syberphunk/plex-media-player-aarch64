#ifndef MENUCOMPONENT_H
#define MENUCOMPONENT_H

#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "ComponentManager.h"
#include "utils/Utils.h"

class MenuComponent : public ComponentBase
{
  Q_OBJECT
  DEFINE_SINGLETON(MenuComponent);

public:
  const char* componentName() override { return "menu"; }
  bool componentExport() override { return false; }
  bool componentInitialize() override;
  void componentPostInitialize() override;

  Q_PROPERTY(bool visible READ visible NOTIFY visibleChanged)
  Q_PROPERTY(QString menuKey READ menuKey NOTIFY configChanged)
  Q_PROPERTY(QString menuButton READ menuButton NOTIFY configChanged)
  Q_PROPERTY(bool keyboardGrabbed READ keyboardGrabbed NOTIFY keyboardGrabbedChanged)
  Q_PROPERTY(qreal textScale READ textScale NOTIFY configChanged)
  Q_PROPERTY(QString textSizeName READ textSizeName NOTIFY configChanged)
  Q_PROPERTY(bool highContrast READ highContrast NOTIFY configChanged)
  Q_PROPERTY(QVariantList keyBindings READ keyBindings NOTIFY configChanged)
  Q_PROPERTY(QString downloadDir READ downloadDir NOTIFY downloadChanged)
  Q_PROPERTY(QString downloadPreset READ downloadPreset NOTIFY downloadChanged)
  Q_PROPERTY(bool downloadWritable READ downloadWritable NOTIFY downloadChanged)
  Q_PROPERTY(bool downloadAsk READ downloadAsk NOTIFY downloadChanged)
  Q_PROPERTY(QString downloadStatus READ downloadStatus NOTIFY downloadStatusChanged)

  bool visible() const { return m_visible; }
  QString menuKey() const;
  QString menuButton() const;
  bool keyboardGrabbed() const { return !m_grabbers.isEmpty(); }
  qreal textScale() const;
  QString textSizeName() const;
  bool highContrast() const;
  QVariantList keyBindings() const { return m_keyBindings; }
  QString downloadDir() const;
  QString downloadPreset() const;
  bool downloadWritable() const;
  bool downloadAsk() const;
  QString downloadStatus() const { return m_downloadStatus; }

  Q_INVOKABLE void show();
  Q_INVOKABLE void hide();
  Q_INVOKABLE void toggle();
  Q_INVOKABLE QString presetPath(const QString& preset) const;
  Q_INVOKABLE bool applyDownloadDir(const QString& path);
  Q_INVOKABLE void restoreDefaults(const QString& group);
  Q_INVOKABLE void noteDownload(const QString& name, const QString& state);
  Q_INVOKABLE void grabKeyboard(const QString& owner, bool grab);

  Q_SIGNAL void visibleChanged();
  Q_SIGNAL void keyboardGrabbedChanged();
  Q_SIGNAL void configChanged();
  Q_SIGNAL void downloadChanged();
  Q_SIGNAL void downloadStatusChanged();

private:
  explicit MenuComponent(QObject* parent = nullptr);

  void writeInputMap();
  void writeMapFile(const QString& relPath, const QString& name, const QString& idmatcher,
                    const QString& trigger);
  void loadKeyBindings();
  bool ensureWritable(const QString& path) const;

  Q_SLOT void onSettingsChanged(const QString& section, const QVariantMap& values);

  bool m_visible = false;
  QSet<QString> m_grabbers;
  QString m_downloadStatus;
  QVariantList m_keyBindings;
};

#endif // MENUCOMPONENT_H
