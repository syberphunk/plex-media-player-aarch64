#include "UpdateManager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
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
#define SETTING_UPDATE_CADENCE "updateCadence"
#define SETTING_UPDATE_LAST_CHECK "updateLastCheck"
#define SETTING_UPDATE_REPO_URL "updateRepoUrl"

#define RELEASE_API_URL "https://api.github.com/repos/syberphunk/plex-media-player-aarch64/releases/latest"
#define RELEASE_NOTES_LIMIT (256 * 1024)
#define RELEASE_NOTES_TIMEOUT 15000

#define REPO_STATE_URL "https://syberphunk.github.io/plex-media-player-aarch64/update.json"
#define REPO_STATE_LIMIT (16 * 1024)
#define REPO_STATE_TIMEOUT 15000

#define CHECK_INTERVAL_MS (6 * 60 * 60 * 1000)
#define CHECK_STARTUP_DELAY_MS 4000

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

  QList<int> tagNumbers(const QString& tag)
  {
    static const QRegularExpression digits("\\d+");

    QList<int> numbers;
    QRegularExpressionMatchIterator matches = digits.globalMatch(tag);
    while (matches.hasNext())
      numbers.append(matches.next().captured(0).toInt());

    return numbers;
  }

  bool tagIsNewer(const QString& candidate, const QString& installed)
  {
    const QList<int> left = tagNumbers(candidate);
    const QList<int> right = tagNumbers(installed);

    if (left.isEmpty() || right.isEmpty() || left.size() != right.size())
      return candidate != installed;

    for (int i = 0; i < left.size(); i++)
    {
      if (left[i] != right[i])
        return left[i] > right[i];
    }

    return false;
  }

  bool predatesBuild(const QString& published)
  {
    const QDate stamp = QDateTime::fromString(published, Qt::ISODate).date();
    const QDate built = QDate::fromString(Version::GetBuildDate(), "yyyy-MM-dd");

    if (!stamp.isValid() || !built.isValid())
      return false;

    return stamp < built;
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
  scheduleChecks();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
QString UpdateManager::cadence() const
{
  const QString mode =
    SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, SETTING_UPDATE_CADENCE).toString();

  return mode.isEmpty() ? QString("startup") : mode;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::setCadence(const QString& cadence)
{
  if (cadence == this->cadence())
    return;

  SettingsComponent::Get().setValue(SETTINGS_SECTION_MAIN, SETTING_UPDATE_CADENCE, cadence);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
QString UpdateManager::lastCheck() const
{
  const QString stamp =
    SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, SETTING_UPDATE_LAST_CHECK).toString();

  const QDateTime when = QDateTime::fromString(stamp, Qt::ISODate);
  if (!when.isValid())
    return QString();

  return when.toLocalTime().toString("yyyy-MM-dd HH:mm");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool UpdateManager::taggedBuild() const
{
  return !Version::GetReleaseTag().isEmpty();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool UpdateManager::hasSkipped() const
{
  return !SettingsComponent::Get()
            .value(SETTINGS_SECTION_MAIN, SETTING_UPDATE_SKIPPED)
            .toString()
            .isEmpty();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::forgetSkipped()
{
  SettingsComponent::Get().setValue(SETTINGS_SECTION_MAIN, SETTING_UPDATE_SKIPPED, QString());
  QLOG_INFO() << "Cleared the skipped version";
  emit availableVersionChanged();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool UpdateManager::cadenceDue() const
{
  const QString mode = cadence();

  if (mode == QLatin1String("manual"))
    return false;

  if (mode == QLatin1String("startup"))
    return true;

  const QString stamp =
    SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, SETTING_UPDATE_LAST_CHECK).toString();

  const QDateTime last = QDateTime::fromString(stamp, Qt::ISODate);
  if (!last.isValid())
    return true;

  const qint64 days = mode == QLatin1String("weekly") ? 7 : 1;
  return last.secsTo(QDateTime::currentDateTimeUtc()) >= days * 24 * 60 * 60;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::scheduleChecks()
{
  if (!m_checkTimer)
  {
    m_checkTimer = new QTimer(this);
    m_checkTimer->setInterval(CHECK_INTERVAL_MS);
    connect(m_checkTimer, &QTimer::timeout, this, [this]()
    {
      if (checkEnabled() && cadenceDue())
        runCheck(false);
    });
  }

  if (cadence() == QLatin1String("manual"))
  {
    m_checkTimer->stop();
    return;
  }

  m_checkTimer->start();

  if (cadenceDue())
    QTimer::singleShot(CHECK_STARTUP_DELAY_MS, this, [this]()
    {
      if (checkEnabled())
        runCheck(false);
    });
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::runCheck(bool userCheck)
{
  if (fetchRepoState(userCheck))
    return;

  fetchRelease(userCheck);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::checkNow()
{
  runCheck(true);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::setCheckStatus(CheckStatus status, const QString& message)
{
  m_checkStatus = status;
  m_checkMessage = message;
  emit checkStatusChanged();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::readSandboxInfo()
{
  if (!QFile::exists("/.flatpak-info"))
    return;

  QSettings info("/.flatpak-info", QSettings::IniFormat);

  m_appId = info.value("Application/name").toString();
  m_appCommit = info.value("Instance/app-commit").toString();
  const QString appPath = info.value("Instance/app-path").toString();

  m_inSandbox = !m_appId.isEmpty();

  QLOG_INFO() << "Flatpak sandbox:" << m_appId << "commit" << shortCommit(m_appCommit)
              << "release" << Version::GetReleaseTag() << "app path" << appPath;
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
  const QString tag = Version::GetReleaseTag();
  if (!tag.isEmpty())
    return tag;

  return Version::GetVersionString();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::setState(State state)
{
  if (m_state == state)
    return;

  m_state = state;
  emit stateChanged();
  emit availableVersionChanged();
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
  if (section != SETTINGS_SECTION_MAIN)
    return;

  if (values.contains(SETTING_UPDATE_CADENCE))
  {
    emit cadenceChanged();

    if (checkEnabled())
      scheduleChecks();
  }

  if (!values.contains(SETTING_UPDATE_CHECK))
    return;

  if (values[SETTING_UPDATE_CHECK].toBool())
  {
    if (m_inSandbox && m_monitorPath.isEmpty())
      startMonitor();

    scheduleChecks();
  }
  else
  {
    stopMonitor();

    if (m_checkTimer)
      m_checkTimer->stop();

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
  if (m_state == StateUpdating || m_state == StateDone)
  {
    QLOG_INFO() << "Ignoring an update report while the install is in progress";
    return;
  }

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
  if (!skipped.isEmpty() && (skipped == m_remoteCommit || skipped == m_availableTag))
  {
    QLOG_INFO() << "Skipping" << shortCommit(m_remoteCommit) << "at the user's request";
    return;
  }

  if (!m_repoTag.isEmpty() && m_repoCommit == m_remoteCommit)
    m_availableVersion = m_repoTag;
  else
    m_availableVersion = m_availableTag.isEmpty() ? shortCommit(m_remoteCommit) : m_availableTag;

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
    {
      QLOG_INFO() << "Portal reported nothing to install, running"
                  << shortCommit(m_runningCommit) << "local" << shortCommit(m_localCommit);

      if (!m_localCommit.isEmpty() && !m_runningCommit.isEmpty()
          && m_localCommit != m_runningCommit)
      {
        setState(StateDone);
        break;
      }

      const QString name = !m_availableVersion.isEmpty() ? m_availableVersion : m_availableTag;
      m_errorMessage = name.isEmpty()
                         ? QString("The remote this copy was installed from had nothing to install")
                         : QString("%1 is published, but the remote this copy was installed from "
                                   "does not carry it yet")
                             .arg(name);
      setState(StateFailed);
      break;
    }

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
  if (m_notesStatus == NotesReady)
    return;

  fetchRelease(false);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
QString UpdateManager::repoStateUrl() const
{
  const QString configured =
    SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, SETTING_UPDATE_REPO_URL).toString();

  return configured.isEmpty() ? QString(REPO_STATE_URL) : configured;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool UpdateManager::fetchRepoState(bool userCheck)
{
  if (m_appCommit.isEmpty())
  {
    QLOG_INFO() << "No sandbox commit to compare, leaving detection to the release feed";
    m_repoAnswer = RepoUnknown;
    return false;
  }

  if (m_stateReply)
  {
    if (userCheck)
      setCheckStatus(CheckBusy, QString("Checking for updates"));
    return true;
  }

  const QUrl url(repoStateUrl());
  if (!url.isValid() || url.scheme() != QLatin1String("https"))
  {
    QLOG_ERROR() << "Refusing to read the update remote over" << url.scheme();
    m_repoAnswer = RepoUnknown;
    return false;
  }

  m_stateRetried = false;
  m_stateUserCheck = userCheck;
  startStateRequest(true);

  setCheckStatus(CheckBusy, QString("Checking for updates"));
  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::startStateRequest(bool allowHttp2)
{
  const QUrl url(repoStateUrl());

  if (!m_network)
    m_network = new QNetworkAccessManager(this);

  QNetworkRequest request(url);
  request.setRawHeader("Accept", "application/json");
  request.setRawHeader("User-Agent", SystemComponent::Get().getUserAgent().toUtf8());
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setAttribute(QNetworkRequest::Http2AllowedAttribute, allowHttp2);
  request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                       QNetworkRequest::AlwaysNetwork);
  request.setTransferTimeout(REPO_STATE_TIMEOUT);

  m_stateReply = m_network->get(request);
  connect(m_stateReply, &QNetworkReply::finished, this, &UpdateManager::onStateFinished);
  connect(m_stateReply, &QNetworkReply::readyRead, this, [this]() {
    if (m_stateReply && m_stateReply->bytesAvailable() > REPO_STATE_LIMIT)
    {
      QLOG_WARN() << "Update remote state exceeds" << REPO_STATE_LIMIT << "bytes, aborting";
      m_stateReply->abort();
    }
  });
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::onStateFinished()
{
  QNetworkReply* reply = m_stateReply;
  m_stateReply = nullptr;
  if (!reply)
    return;

  reply->deleteLater();

  const bool userCheck = m_stateUserCheck;
  m_stateUserCheck = false;

  if (reply->error() != QNetworkReply::NoError)
  {
    QLOG_WARN() << "Update remote state request failed:" << reply->errorString();

    if (!m_stateRetried && reply->error() != QNetworkReply::OperationCanceledError)
    {
      m_stateRetried = true;
      m_stateUserCheck = userCheck;
      QLOG_INFO() << "Retrying the update remote state without HTTP/2";
      startStateRequest(false);
      return;
    }

    m_repoAnswer = RepoUnknown;
    fetchRelease(userCheck);
    return;
  }

  const QByteArray body = reply->read(REPO_STATE_LIMIT);
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(body, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject())
  {
    QLOG_WARN() << "Update remote state is not an object:" << error.errorString();
    m_repoAnswer = RepoUnknown;
    fetchRelease(userCheck);
    return;
  }

  const QJsonObject state = document.object();
  const QString ref = state.value("ref").toString();
  const QString commit = state.value("commit").toString();
  const QString tag = state.value("tag").toString();
  const QString published = state.value("published").toString();

  if (!ref.isEmpty() && !m_appId.isEmpty() && !ref.contains(m_appId))
  {
    QLOG_WARN() << "Update remote state names" << ref << "not" << m_appId;
    m_repoAnswer = RepoUnknown;
    fetchRelease(userCheck);
    return;
  }

  if (commit.isEmpty())
  {
    QLOG_WARN() << "Update remote state names no commit";
    m_repoAnswer = RepoUnknown;
    fetchRelease(userCheck);
    return;
  }

  if (predatesBuild(published))
  {
    QLOG_INFO() << "Update remote state from" << published << "predates this build, built"
                << Version::GetBuildDate();
    m_repoAnswer = RepoCurrent;

    if (m_state == StateIdle)
      setCheckStatus(CheckUpToDate, QString("%1 is newer than the update remote")
                                      .arg(installedVersion()));

    fetchReleaseNotes();
    return;
  }

  considerCommit(commit, tag, userCheck);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::considerCommit(const QString& commit, const QString& tag, bool userCheck)
{
  m_repoCommit = commit;
  m_repoTag = tag;

  SettingsComponent::Get().setValue(SETTINGS_SECTION_MAIN, SETTING_UPDATE_LAST_CHECK,
                                    QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

  if (commit == m_appCommit)
  {
    m_repoAnswer = RepoCurrent;
    QLOG_INFO() << "Update remote carries" << shortCommit(commit) << "which is what is running";

    if (m_state == StateIdle)
      setCheckStatus(CheckUpToDate, QString("%1 is the latest build on the update remote")
                                      .arg(installedVersion()));

    fetchReleaseNotes();
    return;
  }

  m_repoAnswer = RepoAhead;
  m_runningCommit = m_appCommit;
  m_remoteCommit = commit;

  QLOG_INFO() << "Update remote carries" << shortCommit(commit) << "against running"
              << shortCommit(m_appCommit) << "tag" << (tag.isEmpty() ? QString("none") : tag);

  if (m_state == StateUpdating || m_state == StateDone)
    return;

  if (!m_localCommit.isEmpty() && m_localCommit == commit && m_localCommit != m_appCommit)
  {
    m_availableVersion = tag.isEmpty() ? shortCommit(commit) : tag;
    emit availableVersionChanged();
    setCheckStatus(CheckIdle);
    setState(StateDone);
    fetchReleaseNotes();
    return;
  }

  const QString skipped =
    SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, SETTING_UPDATE_SKIPPED).toString();

  if (!skipped.isEmpty() && !userCheck && (skipped == commit || (!tag.isEmpty() && skipped == tag)))
  {
    QLOG_INFO() << "Skipping" << shortCommit(commit) << "at the user's request";
    setCheckStatus(CheckIdle, QString("%1 was skipped")
                                .arg(tag.isEmpty() ? shortCommit(commit) : tag));
    return;
  }

  m_availableVersion = tag.isEmpty() ? shortCommit(commit) : tag;
  emit availableVersionChanged();
  setCheckStatus(CheckIdle);
  setState(StateAvailable);

  fetchReleaseNotes();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::fetchRelease(bool userCheck)
{
  if (userCheck)
    m_userCheck = true;

  if (m_notesReply)
  {
    if (userCheck)
      setCheckStatus(CheckBusy, QString("Checking for updates"));
    return;
  }

  const QUrl url(RELEASE_API_URL);
  if (url.scheme() != QLatin1String("https"))
  {
    QLOG_ERROR() << "Refusing to fetch release notes over" << url.scheme();
    setNotesStatus(NotesFailed);
    setCheckStatus(CheckFailed, QString("The release feed must use https"));
    return;
  }

  m_notesRetried = false;
  startNotesRequest(true);

  setNotesStatus(NotesLoading);
  setCheckStatus(CheckBusy, QString("Checking for updates"));
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::startNotesRequest(bool allowHttp2)
{
  const QUrl url(RELEASE_API_URL);

  if (!m_network)
    m_network = new QNetworkAccessManager(this);

  QNetworkRequest request(url);
  request.setRawHeader("Accept", "application/vnd.github+json");
  request.setRawHeader("User-Agent", SystemComponent::Get().getUserAgent().toUtf8());
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setAttribute(QNetworkRequest::Http2AllowedAttribute, allowHttp2);
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
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::onNotesFinished()
{
  QNetworkReply* reply = m_notesReply;
  m_notesReply = nullptr;
  if (!reply)
    return;

  reply->deleteLater();

  const bool userCheck = m_userCheck;
  m_userCheck = false;

  if (reply->error() != QNetworkReply::NoError)
  {
    QLOG_WARN() << "Release notes request failed:" << reply->errorString();

    if (!m_notesRetried && reply->error() != QNetworkReply::OperationCanceledError)
    {
      m_notesRetried = true;
      m_userCheck = userCheck;
      QLOG_INFO() << "Retrying the release feed without HTTP/2";
      startNotesRequest(false);
      return;
    }

    setNotesStatus(NotesFailed);
    setCheckStatus(CheckFailed, QString("Could not reach the release feed"));
    return;
  }

  const QByteArray body = reply->read(RELEASE_NOTES_LIMIT);
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(body, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject())
  {
    QLOG_WARN() << "Release notes are not an object:" << error.errorString();
    setNotesStatus(NotesFailed);
    setCheckStatus(CheckFailed, QString("The release feed was unreadable"));
    return;
  }

  const QJsonObject release = document.object();
  const QString tag = release.value("tag_name").toString();
  const QString notes = release.value("body").toString();

  m_releaseNoteBlocks = blockify(notes);
  m_releaseNotes = flatten(m_releaseNoteBlocks);
  setNotesStatus(m_releaseNoteBlocks.isEmpty() ? NotesFailed : NotesReady);

  SettingsComponent::Get().setValue(SETTINGS_SECTION_MAIN, SETTING_UPDATE_LAST_CHECK,
                                    QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

  if (tag.isEmpty())
  {
    setCheckStatus(CheckFailed, QString("The release feed named no version"));
    return;
  }

  m_availableTag = tag;

  if (m_state == StateAvailable || m_state == StateUpdating || m_state == StateDone)
  {
    m_availableVersion = m_repoTag.isEmpty() ? tag : m_repoTag;
    emit availableVersionChanged();
    setCheckStatus(CheckIdle);
    return;
  }

  if (m_repoAnswer != RepoUnknown)
    return;

  considerRelease(tag, userCheck);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void UpdateManager::considerRelease(const QString& tag, bool userCheck)
{
  const QString installed = Version::GetReleaseTag();

  if (installed.isEmpty())
  {
    QLOG_INFO() << "Untagged build, leaving detection to the system update monitor";
    setCheckStatus(CheckIdle, QString("This build is not from a release, so the latest release "
                                      "is shown for reference"));
    return;
  }

  if (!tagIsNewer(tag, installed))
  {
    QLOG_INFO() << "Running" << installed << "against feed release" << tag;
    setCheckStatus(CheckUpToDate, QString("%1 is the latest release").arg(installed));
    return;
  }

  const QString skipped =
    SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, SETTING_UPDATE_SKIPPED).toString();

  if (!skipped.isEmpty() && skipped == tag && !userCheck)
  {
    QLOG_INFO() << "Skipping" << tag << "at the user's request";
    setCheckStatus(CheckIdle, QString("%1 was skipped").arg(tag));
    return;
  }

  QLOG_INFO() << "Release" << tag << "is newer than this build" << installed;

  m_availableVersion = tag;
  emit availableVersionChanged();
  setCheckStatus(CheckIdle);
  setState(StateAvailable);
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

  const QString mark = !m_remoteCommit.isEmpty() ? m_remoteCommit : m_availableTag;

  if (mark.isEmpty())
  {
    setState(StateIdle);
    return;
  }

  SettingsComponent::Get().setValue(SETTINGS_SECTION_MAIN, SETTING_UPDATE_SKIPPED, mark);
  QLOG_INFO() << "Marked" << mark << "as skipped";

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
