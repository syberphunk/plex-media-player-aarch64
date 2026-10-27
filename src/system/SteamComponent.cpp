#include "SteamComponent.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUrl>

#include "QsLog.h"
#include "Paths.h"
#include "SessionEnv.h"
#include "settings/SettingsComponent.h"

namespace
{
  const char* const kShortcutsKey = "shortcuts";
  const char* const kFallbackFlatpakId = "io.github.syberphunk.PlexMediaPlayer";

  quint32 crc32(const QByteArray& data)
  {
    static quint32 table[256];
    static bool built = false;
    if (!built)
    {
      for (quint32 i = 0; i < 256; i++)
      {
        quint32 c = i;
        for (int k = 0; k < 8; k++)
          c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        table[i] = c;
      }
      built = true;
    }

    quint32 c = 0xFFFFFFFFu;
    for (char byte : data)
      c = table[(c ^ static_cast<quint8>(byte)) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
  }

  void appendString(QByteArray& out, const char* key, const QString& value)
  {
    out.append(static_cast<char>(0x01));
    out.append(key);
    out.append('\0');
    out.append(value.toUtf8());
    out.append('\0');
  }

  void appendInt(QByteArray& out, const char* key, quint32 value)
  {
    out.append(static_cast<char>(0x02));
    out.append(key);
    out.append('\0');
    for (int i = 0; i < 4; i++)
      out.append(static_cast<char>((value >> (8 * i)) & 0xFF));
  }

  QString readCString(const QByteArray& data, int& pos)
  {
    const int start = pos;
    while (pos < data.size() && data.at(pos) != '\0')
      pos++;
    const QString value = QString::fromUtf8(data.mid(start, pos - start));
    if (pos < data.size())
      pos++;
    return value;
  }
}

SteamComponent::SteamComponent(QObject* parent) : ComponentBase(parent)
{
}

bool SteamComponent::componentInitialize()
{
  m_launchedFromSteam = SessionEnv::launchedFromSteam();
  refresh();
  return true;
}

void SteamComponent::componentPostInitialize()
{
  QLOG_INFO() << "Steam:" << qPrintable(report());
}

QString SteamComponent::ourFlatpakId()
{
  const QString id = QString::fromLocal8Bit(qgetenv("FLATPAK_ID")).trimmed();
  return id.isEmpty() ? QString(kFallbackFlatpakId) : id;
}

QString SteamComponent::exportedIconPath(const QString& flatpakId)
{
  const QStringList roots = {
    QDir::homePath() + "/.local/share/flatpak/exports/share/icons/hicolor",
    "/var/lib/flatpak/exports/share/icons/hicolor"
  };
  for (const QString& root : roots)
  {
    const QStringList sizes = {"256x256", "128x128", "scalable", "512x512", "64x64"};
    for (const QString& size : sizes)
    {
      const QString ext = size == QLatin1String("scalable") ? QString(".svg") : QString(".png");
      const QString path = QString("%1/%2/apps/%3%4").arg(root, size, flatpakId, ext);
      if (QFile::exists(path))
        return path;
    }
  }
  return QString();
}

QString SteamComponent::findSteamRoot()
{
  const QString home = QDir::homePath();
  const QStringList candidates = {
    home + "/.steam/steam",
    home + "/.local/share/Steam",
    home + "/.steam/debian-installation",
    home + "/.var/app/com.valvesoftware.Steam/.local/share/Steam"
  };
  for (const QString& path : candidates)
  {
    if (QFileInfo(path + "/userdata").isDir())
      return QDir(path).canonicalPath();
  }
  return QString();
}

QString SteamComponent::findShortcutsFile(const QString& steamRoot)
{
  if (steamRoot.isEmpty())
    return QString();

  QDir userdata(steamRoot + "/userdata");
  const QStringList ids = userdata.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

  QString best;
  QDateTime bestTime;
  QString fallback;
  for (const QString& id : ids)
  {
    if (id == QLatin1String("0") || id == QLatin1String("anonymous"))
      continue;
    const QString config = userdata.absoluteFilePath(id) + "/config";
    const QString path = config + "/shortcuts.vdf";
    const QFileInfo info(path);
    if (info.isFile())
    {
      if (best.isEmpty() || info.lastModified() > bestTime)
      {
        best = path;
        bestTime = info.lastModified();
      }
    }
    else if (fallback.isEmpty() && QFileInfo(config).isDir())
    {
      fallback = path;
    }
  }
  return best.isEmpty() ? fallback : best;
}

QList<SteamComponent::Entry> SteamComponent::parseShortcuts(const QByteArray& data, bool* ok)
{
  QList<Entry> entries;
  if (ok)
    *ok = false;

  int pos = 0;
  if (pos >= data.size() || data.at(pos) != 0x00)
    return entries;
  pos++;
  if (readCString(data, pos).compare(QLatin1String(kShortcutsKey), Qt::CaseInsensitive) != 0)
    return entries;

  while (pos < data.size())
  {
    const char type = data.at(pos);
    if (type == 0x08)
    {
      if (ok)
        *ok = true;
      break;
    }
    if (type != 0x00)
      return entries;
    pos++;

    Entry entry;
    bool indexOk = false;
    entry.index = readCString(data, pos).toInt(&indexOk);
    if (!indexOk)
      return entries;

    int depth = 0;
    while (pos < data.size())
    {
      const char fieldType = data.at(pos);
      if (fieldType == 0x08)
      {
        pos++;
        if (depth == 0)
          break;
        depth--;
        continue;
      }
      pos++;
      const QString key = readCString(data, pos);
      if (fieldType == 0x00)
      {
        depth++;
        continue;
      }
      if (fieldType == 0x01)
      {
        const QString value = readCString(data, pos);
        if (depth > 0)
          continue;
        if (key.compare(QLatin1String("appname"), Qt::CaseInsensitive) == 0)
          entry.appName = value;
        else if (key.compare(QLatin1String("exe"), Qt::CaseInsensitive) == 0)
          entry.exe = value;
        else if (key.compare(QLatin1String("LaunchOptions"), Qt::CaseInsensitive) == 0)
          entry.launchOptions = value;
      }
      else if (fieldType == 0x02)
      {
        if (pos + 4 > data.size())
          return entries;
        quint32 value = 0;
        for (int i = 0; i < 4; i++)
          value |= static_cast<quint32>(static_cast<quint8>(data.at(pos + i))) << (8 * i);
        pos += 4;
        if (depth == 0 && key.compare(QLatin1String("appid"), Qt::CaseInsensitive) == 0)
          entry.appId = value;
      }
      else
      {
        return entries;
      }
    }

    entries.append(entry);
  }

  return entries;
}

quint32 SteamComponent::shortcutAppId(const QString& exe, const QString& appName)
{
  const QByteArray key = exe.toUtf8() + appName.toUtf8();
  return crc32(key) | 0x80000000u;
}

quint64 SteamComponent::runGameId(quint32 appId)
{
  if (appId == 0)
    return 0;
  return (static_cast<quint64>(appId) << 32) | 0x02000000ull;
}

void SteamComponent::refresh()
{
  m_lastError.clear();
  m_shortcutIndex = -1;
  m_entryCount = 0;
  m_shortcutName.clear();
  m_runGameId = 0;

  m_steamRoot = findSteamRoot();
  m_shortcutsPath = findShortcutsFile(m_steamRoot);

  if (m_steamRoot.isEmpty())
  {
    m_lastError = tr("No Steam installation is visible to the app.");
    emit shortcutChanged();
    return;
  }

  QFile file(m_shortcutsPath);
  if (!m_shortcutsPath.isEmpty() && file.exists())
  {
    if (!file.open(QIODevice::ReadOnly))
    {
      m_lastError = tr("Steam's shortcut list could not be read.");
      emit shortcutChanged();
      return;
    }
    const QByteArray data = file.readAll();
    file.close();

    bool parsed = false;
    const QList<Entry> entries = parseShortcuts(data, &parsed);
    if (!parsed)
    {
      m_lastError = tr("Steam's shortcut list is in a format the app does not recognise.");
      emit shortcutChanged();
      return;
    }

    m_entryCount = entries.size();
    const QString flatpakId = ourFlatpakId();
    for (const Entry& entry : entries)
    {
      const QString haystack = entry.exe + " " + entry.launchOptions;
      if (!haystack.contains(flatpakId) && !haystack.contains(QLatin1String("plexmediaplayer")))
        continue;
      m_shortcutIndex = entry.index;
      m_shortcutName = entry.appName;
      const quint32 appId = entry.appId != 0
                              ? entry.appId
                              : shortcutAppId(entry.exe, entry.appName);
      m_runGameId = runGameId(appId);
      break;
    }
  }

  emit shortcutChanged();
}

bool SteamComponent::shouldOffer() const
{
  if (m_launchedFromSteam || !steamPresent() || shortcutPresent())
    return false;
  return SessionEnv::insideGamescope();
}

bool SteamComponent::shouldOfferRelaunch() const
{
  if (m_launchedFromSteam || !canRelaunch())
    return false;
  return SessionEnv::insideGamescope();
}

bool SteamComponent::backup(const QByteArray& data) const
{
  const QString path = Paths::dataDir(
    QString("shortcuts.vdf.%1.bak").arg(QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss")));
  QFile out(path);
  if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  const bool written = out.write(data) == data.size();
  out.close();
  if (written)
  {
    QLOG_INFO() << "Steam: shortcut list backed up to" << qPrintable(path);
  }
  return written;
}

QByteArray SteamComponent::serializeEntry(int index, const Entry& entry)
{
  QByteArray out;
  out.append(static_cast<char>(0x00));
  out.append(QByteArray::number(index));
  out.append('\0');

  appendInt(out, "appid", entry.appId);
  appendString(out, "AppName", entry.appName);
  appendString(out, "Exe", entry.exe);
  appendString(out, "StartDir", QStringLiteral("\"/usr/bin/\""));
  appendString(out, "icon", exportedIconPath(ourFlatpakId()));
  appendString(out, "ShortcutPath", QString());
  appendString(out, "LaunchOptions", entry.launchOptions);
  appendInt(out, "IsHidden", 0);
  appendInt(out, "AllowDesktopConfig", 1);
  appendInt(out, "AllowOverlay", 1);
  appendInt(out, "OpenVR", 0);
  appendInt(out, "Devkit", 0);
  appendString(out, "DevkitGameID", QString());
  appendInt(out, "DevkitOverrideAppID", 0);
  appendInt(out, "LastPlayTime", 0);
  appendString(out, "FlatpakAppID", ourFlatpakId());

  out.append(static_cast<char>(0x00));
  out.append("tags");
  out.append('\0');
  out.append(static_cast<char>(0x08));

  out.append(static_cast<char>(0x08));
  return out;
}

bool SteamComponent::createShortcut()
{
  m_lastError.clear();

  if (m_shortcutsPath.isEmpty())
  {
    m_lastError = tr("No Steam user profile is visible to the app.");
    emit shortcutChanged();
    return false;
  }

  const QString flatpakId = ourFlatpakId();
  Entry entry;
  entry.appName = QStringLiteral("Plex Media Player");
  entry.exe = QStringLiteral("\"/usr/bin/flatpak\"");
  entry.launchOptions = QString("run --branch=%1 %2")
                          .arg(QString::fromLocal8Bit(qgetenv("FLATPAK_BRANCH")).isEmpty()
                                 ? QStringLiteral("master")
                                 : QString::fromLocal8Bit(qgetenv("FLATPAK_BRANCH")),
                               flatpakId);
  entry.appId = shortcutAppId(entry.exe, entry.appName);

  QByteArray data;
  QFile file(m_shortcutsPath);
  const bool existed = file.exists();
  if (existed)
  {
    if (!file.open(QIODevice::ReadOnly))
    {
      m_lastError = tr("Steam's shortcut list could not be read.");
      emit shortcutChanged();
      return false;
    }
    data = file.readAll();
    file.close();

    bool parsed = false;
    const QList<Entry> entries = parseShortcuts(data, &parsed);
    if (!parsed || data.size() < 2
        || data.at(data.size() - 1) != 0x08 || data.at(data.size() - 2) != 0x08)
    {
      m_lastError = tr("Steam's shortcut list is in a format the app does not recognise.");
      emit shortcutChanged();
      return false;
    }
    if (!backup(data))
    {
      m_lastError = tr("The existing shortcut list could not be backed up.");
      emit shortcutChanged();
      return false;
    }

    int next = 0;
    for (const Entry& existing : entries)
      next = qMax(next, existing.index + 1);
    entry.index = next;
    data = data.left(data.size() - 2) + serializeEntry(next, entry);
    data.append(static_cast<char>(0x08));
    data.append(static_cast<char>(0x08));
  }
  else
  {
    QDir().mkpath(QFileInfo(m_shortcutsPath).absolutePath());
    entry.index = 0;
    data.append(static_cast<char>(0x00));
    data.append(kShortcutsKey);
    data.append('\0');
    data.append(serializeEntry(0, entry));
    data.append(static_cast<char>(0x08));
    data.append(static_cast<char>(0x08));
  }

  QSaveFile out(m_shortcutsPath);
  if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit())
  {
    m_lastError = tr("Steam's shortcut list could not be written.");
    QLOG_ERROR() << "Steam: writing" << qPrintable(m_shortcutsPath) << "failed:"
                 << qPrintable(out.errorString());
    emit shortcutChanged();
    return false;
  }

  QLOG_INFO() << "Steam: wrote shortcut" << entry.index << "appid" << entry.appId
              << "to" << qPrintable(m_shortcutsPath);
  refresh();
  return shortcutPresent();
}

bool SteamComponent::relaunchThroughSteam()
{
  if (!canRelaunch())
  {
    m_lastError = tr("There is no Steam shortcut to start.");
    emit shortcutChanged();
    return false;
  }

  const QUrl url(QString("steam://rungameid/%1").arg(m_runGameId));
  QLOG_INFO() << "Steam: starting" << qPrintable(url.toString());
  if (!QDesktopServices::openUrl(url))
  {
    m_lastError = tr("Steam did not answer the request to start the shortcut.");
    QLOG_WARN() << "Steam: the system did not handle" << qPrintable(url.toString());
    emit shortcutChanged();
    return false;
  }
  return true;
}

QString SteamComponent::report() const
{
  QStringList parts;
  parts << QString("launched-from-steam=%1").arg(m_launchedFromSteam ? "yes" : "no");
  if (!SessionEnv::steamGameId().isEmpty())
    parts << QString("game-id=%1").arg(SessionEnv::steamGameId());
  parts << QString("steam-root=%1").arg(m_steamRoot.isEmpty() ? QString("(none)") : m_steamRoot);
  parts << QString("shortcuts=%1").arg(m_shortcutsPath.isEmpty() ? QString("(none)")
                                                                 : m_shortcutsPath);
  parts << QString("entries=%1").arg(m_entryCount);
  if (m_shortcutIndex >= 0)
  {
    parts << QString("ours=%1 \"%2\" rungameid=%3")
               .arg(m_shortcutIndex).arg(m_shortcutName).arg(m_runGameId);
  }
  else
  {
    parts << "ours=none";
  }
  if (!m_lastError.isEmpty())
    parts << QString("error=\"%1\"").arg(m_lastError);
  return parts.join(" ");
}
