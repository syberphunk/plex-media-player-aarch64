#include "UpdateManager.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>

#ifdef LINUX_DBUS
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusMetaType>
#include <QtDBus/QDBusObjectPath>
#include <QtDBus/QDBusPendingCall>
#include <QtDBus/QDBusPendingCallWatcher>
#include <QtDBus/QDBusPendingReply>
#include <QtDBus/QDBusUnixFileDescriptor>
#endif

#include "QsLog.h"
#include "Version.h"
#include "input/InputComponent.h"
#include "settings/SettingsComponent.h"
#include "system/SystemComponent.h"

#define PORTAL_SERVICE "org.freedesktop.portal.Flatpak"
#define PORTAL_PATH "/org/freedesktop/portal/Flatpak"
#define PORTAL_INTERFACE "org.freedesktop.portal.Flatpak"
#define MONITOR_INTERFACE "org.freedesktop.portal.Flatpak.UpdateMonitor"

#define FLATPAK_SPAWN_FLAGS_LATEST_VERSION 2

#define SETTING_UPDATE_CHECK "updateCheck"
#define SETTING_UPDATE_SKIPPED "updateSkipped"

#define RELEASE_API_URL "https://api.github.com/repos/syberphunk/plex-media-player-aarch64/releases/latest"
#define RELEASE_NOTES_LIMIT (256 * 1024)
#define RELEASE_NOTES_TIMEOUT 15000

namespace
{
  enum PortalStatus
  {
    PortalRunning = 0,
    PortalEmpty = 1,
    PortalDone = 2,
    PortalFailed = 3
  };

  QString shortCommit(const QString& commit)
  {
    return commit.left(12);
  }

  QVariantList blockify(const QString& markdown)
  {
    static const QRegularExpression newline("\r\n|\n|\r");
    static const QRegularExpression heading("^\\s*#{1,6}\\s+");
    static const QRegularExpression bullet("^\\s*[-*]\\s+");
    static const QRegularExpression link("\\[([^\\]]+)\\]\\([^)]*\\)");

    QVariantList blocks;
    QString current;

    auto flush = [&blocks, &current]()
    {
      if (current.isEmpty())
        return;

      QVariantMap block;
      block["heading"] = false;
      block["text"] = current;
      blocks << block;
      current.clear();
    };

    const QStringList lines = markdown.split(newline);

    for (const QString& line : lines)
    {
      QString text = line;
      text.remove(QLatin1Char('`'));
      text.remove(QLatin1String("**"));
      text.replace(link, QLatin1String("\\1"));

      if (text.trimmed().isEmpty())
      {
        flush();
        continue;
      }

      if (heading.match(text).hasMatch())
      {
        flush();

        QVariantMap block;
        block["heading"] = true;
        block["text"] = text.remove(heading).trimmed();
        blocks << block;
        continue;
      }

      if (bullet.match(text).hasMatch())
      {
        flush();
        current = QString::fromUtf8("• ") + text.remove(bullet).trimmed();
        continue;
      }

      if (current.isEmpty())
        current = text.trimmed();
      else
        current += QLatin1Char(' ') + text.trimmed();
    }

    flush();
    return blocks;
  }

  QString flatten(const QVariantList& blocks)
  {
    QStringList out;

    for (const QVariant& entry : blocks)
      out << entry.toMap().value("text").toString();

    return out.join(QLatin1Char('\n'));
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
UpdateManager::UpdateManager(QObject* parent) : ComponentBase(parent)
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool UpdateManager::componentInitialize()
{
  readSandboxInfo();
  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::componentPostInitialize()
{
  connect(&SettingsComponent::Get(), &SettingsComponent::sectionValueUpdate,
          this, &UpdateManager::onSettingsChanged);

  InputComponent::Get().registerHostCommand("showUpdate", [this]() { toggleInfo(); });

  if (!m_inSandbox)
  {
    QLOG_INFO() << "Not running in a flatpak sandbox, update checks disabled";
    return;
  }

  if (!checkEnabled())
  {
    QLOG_INFO() << "Update checks are turned off";
    return;
  }

  startMonitor();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::readSandboxInfo()
{
  if (!QFile::exists("/.flatpak-info"))
    return;

  QSettings info("/.flatpak-info", QSettings::IniFormat);

  m_appId = info.value("Application/name").toString();
  const QString appPath = info.value("Instance/app-path").toString();

  m_inSandbox = !m_appId.isEmpty();

  QLOG_INFO() << "Flatpak sandbox:" << m_appId << "app path" << appPath;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool UpdateManager::checkEnabled() const
{
  return SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, SETTING_UPDATE_CHECK).toBool();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
QString UpdateManager::manualCommand() const
{
  if (m_appId.isEmpty())
    return QString();

  return QString("flatpak update %1").arg(m_appId);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
QString UpdateManager::permissionCommand() const
{
  if (m_appId.isEmpty())
    return QString();

  return QString("flatpak permission-set flatpak updates %1 yes").arg(m_appId);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
QString UpdateManager::installedVersion() const
{
  return Version::GetVersionString();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::setState(State state)
{
  if (m_state == state)
    return;

  m_state = state;
  emit stateChanged();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::setNotesStatus(NotesStatus status)
{
  m_notesStatus = status;
  emit releaseNotesChanged();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::onSettingsChanged(const QString& section, const QVariantMap& values)
{
  if (section != SETTINGS_SECTION_MAIN || !values.contains(SETTING_UPDATE_CHECK))
    return;

  if (values[SETTING_UPDATE_CHECK].toBool())
  {
    if (m_inSandbox && m_monitorPath.isEmpty())
      startMonitor();
  }
  else
  {
    stopMonitor();
    if (m_state == StateAvailable)
      setState(StateIdle);
  }

  emit checkEnabledChanged();
}

#ifdef LINUX_DBUS

///////////////////////////////////////////////////////////////////////////////////////////////////
bool UpdateManager::startMonitor()
{
  QDBusConnection bus = QDBusConnection::sessionBus();
  if (!bus.isConnected())
  {
    QLOG_WARN() << "No session bus, cannot monitor for updates";
    return false;
  }

  QDBusMessage call = QDBusMessage::createMethodCall(PORTAL_SERVICE, PORTAL_PATH,
                                                     PORTAL_INTERFACE, "CreateUpdateMonitor");
  call << QVariant::fromValue(QVariantMap());

  const QDBusMessage reply = bus.call(call, QDBus::Block, 10000);
  if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
  {
    QLOG_WARN() << "CreateUpdateMonitor failed:" << reply.errorName() << reply.errorMessage();
    return false;
  }

  m_monitorPath = reply.arguments().at(0).value<QDBusObjectPath>().path();
  if (m_monitorPath.isEmpty())
  {
    QLOG_WARN() << "CreateUpdateMonitor returned no handle";
    return false;
  }

  bus.connect(PORTAL_SERVICE, m_monitorPath, MONITOR_INTERFACE, "UpdateAvailable",
              this, SLOT(onUpdateAvailable(QVariantMap)));
  bus.connect(PORTAL_SERVICE, m_monitorPath, MONITOR_INTERFACE, "Progress",
              this, SLOT(onProgress(QVariantMap)));

  QLOG_INFO() << "Update monitor:" << m_monitorPath;
  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::stopMonitor()
{
  if (m_monitorPath.isEmpty())
    return;

  QDBusConnection bus = QDBusConnection::sessionBus();
  bus.disconnect(PORTAL_SERVICE, m_monitorPath, MONITOR_INTERFACE, "UpdateAvailable",
                 this, SLOT(onUpdateAvailable(QVariantMap)));
  bus.disconnect(PORTAL_SERVICE, m_monitorPath, MONITOR_INTERFACE, "Progress",
                 this, SLOT(onProgress(QVariantMap)));

  QDBusMessage call = QDBusMessage::createMethodCall(PORTAL_SERVICE, m_monitorPath,
                                                     MONITOR_INTERFACE, "Close");
  bus.asyncCall(call);

  QLOG_INFO() << "Closed update monitor" << m_monitorPath;
  m_monitorPath.clear();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::applyUpdate()
{
  if (m_state != StateAvailable)
    return;

  hideInfo();

  if (m_monitorPath.isEmpty())
  {
    setState(StateManual);
    return;
  }

  QDBusConnection bus = QDBusConnection::sessionBus();
  QDBusMessage call = QDBusMessage::createMethodCall(PORTAL_SERVICE, m_monitorPath,
                                                     MONITOR_INTERFACE, "Update");
  call << QString() << QVariant::fromValue(QVariantMap());

  m_opIndex = 0;
  m_opCount = 0;
  m_opProgress = 0;
  m_errorMessage.clear();
  m_needsPermission = false;
  emit progressChanged();
  setState(StateUpdating);

  auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
    QDBusPendingReply<> reply = *w;
    w->deleteLater();

    if (!reply.isError())
      return;

    applyFailed(reply.error().name(), reply.error().message());
  });
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::applyFailed(const QString& name, const QString& message)
{
  QLOG_WARN() << "Update failed:" << name << message;

  if (name == QLatin1String("org.freedesktop.DBus.Error.NotSupported"))
  {
    m_needsPermission = true;
    m_errorMessage.clear();
    setState(StateManual);
    return;
  }

  m_errorMessage = message;
  setState(StateFailed);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::restartApp()
{
  QDBusConnection bus = QDBusConnection::sessionBus();
  if (bus.isConnected() && m_inSandbox)
  {
    qDBusRegisterMetaType<QList<QByteArray>>();
    qDBusRegisterMetaType<QMap<uint, QDBusUnixFileDescriptor>>();
    qDBusRegisterMetaType<QMap<QString, QString>>();

    QList<QByteArray> argv;
    for (const QString& arg : qApp->arguments())
      argv.append(arg.toLocal8Bit());

    QDBusMessage call = QDBusMessage::createMethodCall(PORTAL_SERVICE, PORTAL_PATH,
                                                       PORTAL_INTERFACE, "Spawn");
    call << QByteArray("/")
         << QVariant::fromValue(argv)
         << QVariant::fromValue(QMap<uint, QDBusUnixFileDescriptor>())
         << QVariant::fromValue(QMap<QString, QString>())
         << static_cast<uint>(FLATPAK_SPAWN_FLAGS_LATEST_VERSION)
         << QVariant::fromValue(QVariantMap());

    const QDBusMessage reply = bus.call(call, QDBus::Block, 10000);
    if (reply.type() == QDBusMessage::ReplyMessage)
    {
      QLOG_INFO() << "Spawned the updated version, pid"
                  << (reply.arguments().isEmpty() ? 0 : reply.arguments().at(0).toUInt());
      qApp->quit();
      return;
    }

    QLOG_WARN() << "Portal Spawn failed:" << reply.errorName() << reply.errorMessage();
  }

  SystemComponent::restart();
}

#else // LINUX_DBUS

bool UpdateManager::startMonitor()
{
  return false;
}

void UpdateManager::stopMonitor()
{
}

void UpdateManager::applyUpdate()
{
  setState(StateManual);
}

void UpdateManager::restartApp()
{
  SystemComponent::restart();
}

#endif // LINUX_DBUS

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::onUpdateAvailable(const QVariantMap& info)
{
  m_runningCommit = info.value("running-commit").toString();
  m_localCommit = info.value("local-commit").toString();
  m_remoteCommit = info.value("remote-commit").toString();

  QLOG_INFO() << "Update available: running" << shortCommit(m_runningCommit)
              << "local" << shortCommit(m_localCommit)
              << "remote" << shortCommit(m_remoteCommit);

  if (!m_localCommit.isEmpty() && m_localCommit != m_runningCommit
      && (m_remoteCommit.isEmpty() || m_remoteCommit == m_localCommit))
  {
    m_availableVersion = shortCommit(m_localCommit);
    emit availableVersionChanged();
    setState(StateDone);
    return;
  }

  if (m_remoteCommit.isEmpty() || m_remoteCommit == m_localCommit)
    return;

  const QString skipped =
    SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, SETTING_UPDATE_SKIPPED).toString();
  if (!skipped.isEmpty() && skipped == m_remoteCommit)
  {
    QLOG_INFO() << "Skipping" << shortCommit(m_remoteCommit) << "at the user's request";
    return;
  }

  m_availableVersion = shortCommit(m_remoteCommit);
  emit availableVersionChanged();
  setState(StateAvailable);

  fetchReleaseNotes();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::onProgress(const QVariantMap& info)
{
  m_opCount = info.value("n_ops", m_opCount).toInt();
  m_opIndex = info.value("op", m_opIndex).toInt();
  m_opProgress = info.value("progress", m_opProgress).toInt();
  emit progressChanged();

  const int status = info.value("status", PortalRunning).toInt();
  switch (status)
  {
    case PortalRunning:
      break;

    case PortalEmpty:
      QLOG_INFO() << "Portal reported nothing to install";
      setState(StateIdle);
      break;

    case PortalDone:
      QLOG_INFO() << "Update installed";
      setState(StateDone);
      break;

    case PortalFailed:
      applyFailed(info.value("error").toString(), info.value("error_message").toString());
      break;

    default:
      QLOG_WARN() << "Unknown portal status" << status;
      break;
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::fetchReleaseNotes()
{
  if (m_notesReply || m_notesStatus == NotesReady)
    return;

  const QUrl url(RELEASE_API_URL);
  if (url.scheme() != QLatin1String("https"))
  {
    QLOG_ERROR() << "Refusing to fetch release notes over" << url.scheme();
    setNotesStatus(NotesFailed);
    return;
  }

  if (!m_network)
    m_network = new QNetworkAccessManager(this);

  QNetworkRequest request(url);
  request.setRawHeader("Accept", "application/vnd.github+json");
  request.setRawHeader("User-Agent", SystemComponent::Get().getUserAgent().toUtf8());
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setTransferTimeout(RELEASE_NOTES_TIMEOUT);

  m_notesReply = m_network->get(request);
  connect(m_notesReply, &QNetworkReply::finished, this, &UpdateManager::onNotesFinished);
  connect(m_notesReply, &QNetworkReply::readyRead, this, [this]() {
    if (m_notesReply && m_notesReply->bytesAvailable() > RELEASE_NOTES_LIMIT)
    {
      QLOG_WARN() << "Release notes exceed" << RELEASE_NOTES_LIMIT << "bytes, aborting";
      m_notesReply->abort();
    }
  });

  setNotesStatus(NotesLoading);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::onNotesFinished()
{
  QNetworkReply* reply = m_notesReply;
  m_notesReply = nullptr;
  if (!reply)
    return;

  reply->deleteLater();

  if (reply->error() != QNetworkReply::NoError)
  {
    QLOG_WARN() << "Release notes request failed:" << reply->errorString();
    setNotesStatus(NotesFailed);
    return;
  }

  const QByteArray body = reply->read(RELEASE_NOTES_LIMIT);
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(body, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject())
  {
    QLOG_WARN() << "Release notes are not an object:" << error.errorString();
    setNotesStatus(NotesFailed);
    return;
  }

  const QJsonObject release = document.object();
  const QString tag = release.value("tag_name").toString();
  const QString notes = release.value("body").toString();

  if (!tag.isEmpty())
  {
    QString version = tag;
    if (version.startsWith(QLatin1Char('v')))
      version = version.mid(1);

    m_availableVersion = version;
    emit availableVersionChanged();
  }

  m_releaseNoteBlocks = blockify(notes);
  m_releaseNotes = flatten(m_releaseNoteBlocks);
  setNotesStatus(m_releaseNoteBlocks.isEmpty() ? NotesFailed : NotesReady);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::showInfo()
{
  if (m_notesStatus != NotesReady)
    fetchReleaseNotes();

  if (m_infoVisible)
    return;

  m_infoVisible = true;
  emit infoVisibleChanged();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::hideInfo()
{
  if (!m_infoVisible)
    return;

  m_infoVisible = false;
  emit infoVisibleChanged();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::toggleInfo()
{
  if (m_infoVisible)
    hideInfo();
  else
    showInfo();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::showManual()
{
  hideInfo();
  setState(StateManual);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::dismiss()
{
  setState(StateIdle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::skipVersion()
{
  hideInfo();

  if (m_remoteCommit.isEmpty())
  {
    setState(StateIdle);
    return;
  }

  SettingsComponent::Get().setValue(SETTINGS_SECTION_MAIN, SETTING_UPDATE_SKIPPED, m_remoteCommit);
  QLOG_INFO() << "Marked" << shortCommit(m_remoteCommit) << "as skipped";

  setState(StateManual);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::disableChecks()
{
  SettingsComponent::Get().setValue(SETTINGS_SECTION_MAIN, SETTING_UPDATE_CHECK, false);
  stopMonitor();
  hideInfo();
  setState(StateManual);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::setCheckEnabled(bool enabled)
{
  if (enabled == checkEnabled())
    return;

  SettingsComponent::Get().setValue(SETTINGS_SECTION_MAIN, SETTING_UPDATE_CHECK, enabled);
}
