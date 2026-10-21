#include "MenuComponent.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryFile>

#include "QsLog.h"
#include "input/InputComponent.h"
#include "settings/SettingsComponent.h"
#include "shared/Paths.h"
#include "utils/Utils.h"

#define SETTING_MENU_KEY "menuKey"
#define SETTING_MENU_BUTTON "menuButton"
#define SETTING_MENU_TEXT_SCALE "menuTextScale"
#define SETTING_MENU_HIGH_CONTRAST "menuHighContrast"
#define SETTING_DOWNLOAD_DIR "downloadDir"
#define SETTING_DOWNLOAD_ASK "downloadAsk"

#define MAP_NAME "Plex Media Player Menu"
#define MAP_IDMATCHER "^Keyboard"
#define MAP_FILE "inputmaps/menu.json"

#define PAD_MAP_NAME "Plex Media Player Menu Button"
#define PAD_MAP_IDMATCHER "^(?!Keyboard|socket|roku|LIRC|CEC)"
#define PAD_MAP_FILE "inputmaps/menu-gamepad.json"

namespace
{
  struct Default
  {
    const char* group;
    const char* key;
    QVariant value;
  };

  QList<Default> ourDefaults()
  {
    return {
      { "updates", "updateCheck", true },
      { "updates", "updateSkipped", QString() },
      { "updates", "updateCadence", QString("startup") },
      { "downloads", SETTING_DOWNLOAD_DIR, QString() },
      { "downloads", SETTING_DOWNLOAD_ASK, false },
      { "keys", SETTING_MENU_KEY, QString("F1") },
      { "keys", SETTING_MENU_BUTTON, QString("KEY_BUTTON_7") },
      { "display", SETTING_MENU_TEXT_SCALE, QString("normal") },
      { "display", SETTING_MENU_HIGH_CONTRAST, false },
    };
  }

  QString actionLabel(const QString& action)
  {
    QString text = action;

    if (text.startsWith("host:cycle_setting "))
      return QString("toggle %1").arg(text.mid(19));

    if (text.startsWith("host:"))
      text = text.mid(5);

    text.replace(QLatin1Char('_'), QLatin1Char(' '));
    return text;
  }

  QString keyLabel(const QString& pattern)
  {
    QString text = pattern;
    text.replace(QLatin1String("\\+"), QLatin1String("+"));
    text.replace(QLatin1String("\\/"), QLatin1String("/"));
    text.replace(QLatin1String("\\-"), QLatin1String("-"));
    return text;
  }

  QString padLabel(const QString& trigger)
  {
    static const QMap<QString, QString> names = {
      { "KEY_BUTTON_0", "A" },  { "KEY_BUTTON_1", "B" },    { "KEY_BUTTON_2", "X" },
      { "KEY_BUTTON_3", "Y" },  { "KEY_BUTTON_4", "LB" },   { "KEY_BUTTON_5", "RB" },
      { "KEY_BUTTON_6", "Back" }, { "KEY_BUTTON_7", "Start" },
    };

    if (names.contains(trigger))
      return QString("%1 (%2)").arg(trigger, names.value(trigger));

    return trigger;
  }

  bool plainKey(const QString& pattern)
  {
    static const QRegularExpression meta("[()\\[\\]?|*{}^$%]|\\\\(?![+/-])");
    return !meta.match(pattern).hasMatch();
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
MenuComponent::MenuComponent(QObject* parent) : ComponentBase(parent)
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool MenuComponent::componentInitialize()
{
  writeInputMap();
  loadKeyBindings();
  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void MenuComponent::componentPostInitialize()
{
  connect(&SettingsComponent::Get(), &SettingsComponent::sectionValueUpdate,
          this, &MenuComponent::onSettingsChanged);

  InputComponent::Get().registerHostCommand("showMenu", [this]() { toggle(); });
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void MenuComponent::onSettingsChanged(const QString& section, const QVariantMap& values)
{
  if (section != SETTINGS_SECTION_MAIN)
    return;

  if (values.contains(SETTING_MENU_KEY) || values.contains(SETTING_MENU_BUTTON))
  {
    writeInputMap();
    loadKeyBindings();
    emit configChanged();
  }

  if (values.contains(SETTING_MENU_TEXT_SCALE) || values.contains(SETTING_MENU_HIGH_CONTRAST))
    emit configChanged();

  if (values.contains(SETTING_DOWNLOAD_DIR) || values.contains(SETTING_DOWNLOAD_ASK))
    emit downloadChanged();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void MenuComponent::show()
{
  if (m_visible)
    return;

  m_visible = true;
  emit visibleChanged();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void MenuComponent::hide()
{
  if (!m_visible)
    return;

  m_visible = false;
  emit visibleChanged();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void MenuComponent::toggle()
{
  m_visible = !m_visible;
  emit visibleChanged();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
QString MenuComponent::menuKey() const
{
  const QString key = SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, SETTING_MENU_KEY).toString();
  return key.isEmpty() ? QString("F1") : key;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
QString MenuComponent::menuButton() const
{
  return SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, SETTING_MENU_BUTTON).toString();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void MenuComponent::grabKeyboard(const QString& owner, bool grab)
{
  const bool was = keyboardGrabbed();

  if (grab)
    m_grabbers.insert(owner);
  else
    m_grabbers.remove(owner);

  if (keyboardGrabbed() != was)
    emit keyboardGrabbedChanged();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
qreal MenuComponent::textScale() const
{
  const QString scale =
    SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, SETTING_MENU_TEXT_SCALE).toString();

  if (scale == "large")
    return 1.15;

  if (scale == "larger")
    return 1.3;

  return 1.0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
QString MenuComponent::textSizeName() const
{
  const QString scale =
    SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, SETTING_MENU_TEXT_SCALE).toString();

  if (scale == "large" || scale == "larger")
    return scale;

  return QString("normal");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool MenuComponent::highContrast() const
{
  return SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, SETTING_MENU_HIGH_CONTRAST).toBool();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool MenuComponent::downloadAsk() const
{
  return SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, SETTING_DOWNLOAD_ASK).toBool();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
QString MenuComponent::presetPath(const QString& preset) const
{
  if (preset == "appdata")
    return QDir(Paths::dataDir("downloads")).absolutePath();

  const QString xdg = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
  if (!xdg.isEmpty())
    return QDir(xdg).absolutePath();

  return QDir(QDir::homePath() + "/Downloads").absolutePath();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
QString MenuComponent::downloadDir() const
{
  const QString stored =
    SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, SETTING_DOWNLOAD_DIR).toString();

  if (!stored.isEmpty())
    return QDir(stored).absolutePath();

  return presetPath("downloads");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
QString MenuComponent::downloadPreset() const
{
  const QString stored =
    SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, SETTING_DOWNLOAD_DIR).toString();

  if (stored.isEmpty())
    return QString("downloads");

  if (QDir(stored).absolutePath() == presetPath("appdata"))
    return QString("appdata");

  return QString("custom");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool MenuComponent::ensureWritable(const QString& path) const
{
  if (path.isEmpty())
    return false;

  QDir dir(path);
  if (!dir.exists() && !dir.mkpath("."))
    return false;

  QTemporaryFile probe(dir.filePath(".plex-write-probe-XXXXXX"));
  return probe.open();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool MenuComponent::downloadWritable() const
{
  return ensureWritable(downloadDir());
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool MenuComponent::applyDownloadDir(const QString& path)
{
  QString target = path.trimmed();

  if (target.startsWith("~/"))
    target = QDir::homePath() + target.mid(1);

  if (!target.isEmpty() && !ensureWritable(target))
  {
    m_downloadStatus = QString("Cannot write to %1").arg(target);
    emit downloadStatusChanged();
    QLOG_WARN() << "Download directory not writable:" << target;
    return false;
  }

  if (!target.isEmpty())
    target = QDir(target).absolutePath();

  SettingsComponent::Get().setValue(SETTINGS_SECTION_MAIN, SETTING_DOWNLOAD_DIR, target);

  m_downloadStatus.clear();
  emit downloadStatusChanged();
  emit downloadChanged();
  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void MenuComponent::noteDownload(const QString& name, const QString& state)
{
  if (state == "started")
    m_downloadStatus = QString("Downloading %1").arg(name);
  else if (state == "completed")
    m_downloadStatus = QString("Saved %1").arg(name);
  else if (state == "cancelled")
    m_downloadStatus = QString("Cancelled %1").arg(name);
  else
    m_downloadStatus = QString("Failed %1").arg(name);

  QLOG_INFO() << "Download" << state << name;
  emit downloadStatusChanged();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void MenuComponent::restoreDefaults(const QString& group)
{
  for (const Default& entry : ourDefaults())
  {
    if (group != "all" && group != entry.group)
      continue;

    SettingsComponent::Get().setValue(SETTINGS_SECTION_MAIN, entry.key, entry.value);
  }

  QLOG_INFO() << "Restored defaults for" << group;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void MenuComponent::writeInputMap()
{
  QDir dir(Paths::dataDir());
  if (!dir.exists("inputmaps") && !dir.mkpath("inputmaps"))
  {
    QLOG_WARN() << "Could not create" << dir.filePath("inputmaps");
    return;
  }

  writeMapFile(MAP_FILE, MAP_NAME, MAP_IDMATCHER, menuKey());
  writeMapFile(PAD_MAP_FILE, PAD_MAP_NAME, PAD_MAP_IDMATCHER, menuButton());
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void MenuComponent::writeMapFile(const QString& relPath, const QString& name, const QString& idmatcher,
                                 const QString& trigger)
{
  QJsonObject mapping;
  if (!trigger.isEmpty())
    mapping[trigger] = QString("host:showMenu");

  QJsonObject root;
  root["name"] = name;
  root["idmatcher"] = idmatcher;
  root["mapping"] = mapping;

  const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Indented);
  const QString path = Paths::dataDir(relPath);

  QFile existing(path);
  if (existing.open(QIODevice::ReadOnly))
  {
    const bool same = existing.readAll() == data;
    existing.close();

    if (same)
      return;
  }

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
  {
    QLOG_WARN() << "Could not write" << path << file.errorString();
    return;
  }

  file.write(data);
  file.close();

  QLOG_INFO() << name << "bound to" << (trigger.isEmpty() ? QString("nothing") : trigger);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void MenuComponent::loadKeyBindings()
{
  m_keyBindings.clear();

  QJsonParseError err;
  const QJsonDocument doc = Utils::OpenJsonDocument(":/inputmaps/keyboard.json", &err);
  if (doc.isNull() || !doc.isObject())
  {
    QLOG_WARN() << "Could not read the keyboard input map:" << err.errorString();
    return;
  }

  const QVariantMap mapping = doc.object().value("mapping").toObject().toVariantMap();

  for (auto it = mapping.constBegin(); it != mapping.constEnd(); ++it)
  {
    if (!plainKey(it.key()))
      continue;

    const QString action = it.value().typeId() == QMetaType::QVariantMap
                             ? it.value().toMap().value("short").toString()
                             : it.value().toString();

    if (action.isEmpty() || action.contains(QLatin1Char('%')))
      continue;

    QVariantMap entry;
    entry["key"] = keyLabel(it.key());
    entry["action"] = actionLabel(action);
    m_keyBindings << entry;
  }

  QVariantMap menuEntry;
  menuEntry["key"] = menuKey();
  menuEntry["action"] = QString("this menu");
  m_keyBindings.prepend(menuEntry);

  const QString button = menuButton();
  if (!button.isEmpty())
  {
    QVariantMap padEntry;
    padEntry["key"] = padLabel(button);
    padEntry["action"] = QString("this menu (gamepad)");
    m_keyBindings.insert(1, padEntry);
  }
}
