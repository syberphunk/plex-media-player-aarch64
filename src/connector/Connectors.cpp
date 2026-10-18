#include "Connectors.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <QSysInfo>
#include <QUrl>

#include "Paths.h"
#include "QsLog.h"
#include "Version.h"
#include "display/DisplayComponent.h"
#include "input/InputComponent.h"
#include "player/PlayerComponent.h"
#include "settings/SettingsComponent.h"
#include "settings/SettingsSection.h"
#include "system/SystemComponent.h"
#include "utils/Utils.h"

namespace
{
  QString platformName()
  {
    return QStringLiteral("Linux");
  }

  QString platformVersion()
  {
    const QString version = QSysInfo::productVersion();
    if (version.isEmpty() || version == QStringLiteral("unknown"))
      return QSysInfo::kernelVersion();
    return version;
  }

  QString deviceName()
  {
    const QString hostName = Utils::ComputerName();
    if (hostName.isEmpty())
      return QStringLiteral("Plex Media Player");
    return hostName;
  }

  bool settingsWriteAllowed(const QString& section, const QString& leaf, QVariant& value)
  {
    static const QStringList privileged = { "main/embeddedServer",  "main/remoteControl",
                                            "main/remoteInspector", "main/rokuRemote",
                                            "main/userScripts",     "main/webserverport" };

    if (privileged.contains(section + "/" + leaf))
      return false;

    SettingsSection* target = SettingsComponent::Get().getSection(section);
    if (!target || target->isStorage() || !target->contains(leaf))
      return true;

    const QVariant defaultValue = target->defaultValue(leaf);
    if (defaultValue.isValid() && value.metaType() != defaultValue.metaType())
    {
      QVariant converted = value;
      if (converted.convert(defaultValue.metaType()))
        value = converted;
    }

    return true;
  }
}

SystemConnector::SystemConnector(QObject* parent) : ConnectorBase("system", parent)
{
}

void SystemConnector::trackWindow(QWindow* window)
{
  m_window = window;
  if (!window)
    return;

  connect(window, &QWindow::visibilityChanged, this, [this](QWindow::Visibility visibility) {
    emit onVisibilityChanged(static_cast<int>(visibility));
    emit onVisibilityChangedByUser(static_cast<int>(visibility));
    emit onChanged("visibility", static_cast<int>(visibility));
  });
}

QStringList SystemConnector::readableKeys() const
{
  return QStringList{ "visibility", "isReady", "userAgent", "capabilities", "networkAddresses",
                      "networkPort", "cursorVisible" };
}

bool SystemConnector::readKey(const QString& key, QVariant& out) const
{
  SystemComponent& sys = SystemComponent::Get();

  if (key == "visibility")
  {
    out = m_window ? static_cast<int>(m_window->visibility())
                   : static_cast<int>(QWindow::Windowed);
    return true;
  }
  if (key == "isReady")          { out = m_ready; return true; }
  if (key == "userAgent")        { out = sys.getUserAgent(); return true; }
  if (key == "capabilities")     { out = sys.getCapabilitiesString(); return true; }
  if (key == "networkAddresses") { out = sys.networkAddresses(); return true; }
  if (key == "networkPort")      { out = sys.networkPort(); return true; }
  if (key == "cursorVisible")    { out = sys.cursorVisible(); return true; }

  return ConnectorBase::readKey(key, out);
}

bool SystemConnector::writeKey(const QString& key, const QVariant& value)
{
  if (key == "cursorVisible")
  {
    SystemComponent::Get().setCursorVisibility(value.toBool());
    return true;
  }
  if (key == "isReady")
  {
    m_ready = value.toBool();
    return true;
  }
  if (key == "visibility")
    return false;

  return ConnectorBase::writeKey(key, value);
}

QVariantMap SystemConnector::describe()
{
  trace("describe");
  SystemComponent& sys = SystemComponent::Get();

  QVariantMap d = sys.systemInformation();
  d["userAgent"] = sys.getUserAgent();
  d["capabilities"] = sys.getCapabilitiesString();

  d["product"] = "Plex Media Player";
  d["applicationVersion"] = Version::GetVersionString();
  d["platform"] = platformName();
  d["platformVersion"] = platformVersion();
  d["machineHostName"] = deviceName();

  return Envelope::ok(d);
}

QVariantMap SystemConnector::ready(bool isReady)
{
  trace("ready");
  QLOG_INFO() << "Web client reports ready:" << isReady;
  m_ready = isReady;

  if (isReady)
    SystemComponent::Get().hello(QStringLiteral("modern"));

  return Envelope::ok();
}

QVariantMap SystemConnector::isReady()
{
  trace("isReady");
  return Envelope::ok(m_ready);
}

QVariantMap SystemConnector::exit()
{
  trace("exit");
  SystemComponent::Get().exit();
  return Envelope::ok();
}

QVariantMap SystemConnector::restart()
{
  trace("restart");
  SystemComponent::restart();
  return Envelope::ok();
}

QVariantMap SystemConnector::toggleFullscreen()
{
  trace("toggleFullscreen");
  InputComponent::Get().executeActions(QStringList{ "host:fullscreen" });
  return Envelope::ok();
}

QVariantMap SystemConnector::openExternalUrl(const QString& url)
{
  trace("openExternalUrl", url);
  SystemComponent::Get().openExternalUrl(url);
  return Envelope::ok();
}

QVariantMap SystemConnector::setCursorVisibility(bool visible)
{
  trace("setCursorVisibility");
  SystemComponent::Get().setCursorVisibility(visible);
  return Envelope::ok();
}

QVariantMap SystemConnector::userInformation(const QVariantMap& userModel)
{
  trace("userInformation");
  SystemComponent::Get().userInformation(userModel);
  return Envelope::ok();
}

int SystemConnector::beginDeferred()
{
  return ++m_deferIndex;
}

void SystemConnector::completeDeferred(int index, const QVariantMap& envelope)
{
  emit onDeferredReturnValueAvailable(index, envelope);
}

SettingsConnector::SettingsConnector(QObject* parent) : ConnectorBase("settings", parent)
{
  connect(&SettingsComponent::Get(), &SettingsComponent::sectionValueUpdate,
          this, &SettingsConnector::onSectionValuesUpdated);
  connect(&SettingsComponent::Get(), &SettingsComponent::groupUpdate,
          this, [this](const QString&, const QVariant&) { emit onSettingsDescriptionChanged(); });
}

void SettingsConnector::onSectionValuesUpdated(const QString& section, const QVariantMap& values)
{
  for (auto it = values.constBegin(); it != values.constEnd(); ++it)
  {
    const QString flat = section + "." + it.key();
    emit onChanged(flat, it.value());
  }
}

bool SettingsConnector::split(const QString& key, QString& section, QString& leaf)
{
  const int dot = key.indexOf('.');
  if (dot <= 0 || dot == key.length() - 1)
    return false;

  section = key.left(dot);
  leaf = key.mid(dot + 1);
  return true;
}

QStringList SettingsConnector::readableKeys() const
{
  QStringList out;
  const QVariantMap all = SettingsComponent::Get().allValues().toMap();
  for (auto it = all.constBegin(); it != all.constEnd(); ++it)
  {
    const QVariantMap values = it.value().toMap();
    for (const QString& leaf : values.keys())
      out << it.key() + "." + leaf;
  }
  return out;
}

bool SettingsConnector::readKey(const QString& key, QVariant& out) const
{
  if (key == "webInspectorPort")
  {
    const bool on = SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, "remoteInspector").toBool();
    out = on ? WEB_INSPECTOR_PORT : 0;
    return true;
  }

  QString section, leaf;
  if (split(key, section, leaf))
  {
    out = SettingsComponent::Get().value(section, leaf);
    return out.isValid();
  }

  out = SettingsComponent::Get().allValues(key);
  return out.isValid();
}

bool SettingsConnector::writeKey(const QString& key, const QVariant& value)
{
  if (key == "webInspectorPort")
  {
    if (value.toInt() != 0)
      QLOG_WARN() << "Not enabling the remote inspector from the connector";

    SettingsComponent::Get().setValue(SETTINGS_SECTION_MAIN, "remoteInspector", false);
    return true;
  }

  QString section, leaf;
  if (split(key, section, leaf))
  {
    QVariant checked = value;
    if (!settingsWriteAllowed(section, leaf, checked))
    {
      QLOG_WARN() << "Refusing to set" << key << "from the connector";
      return false;
    }
    SettingsComponent::Get().setValue(section, leaf, checked);
    return true;
  }

  SettingsComponent::Get().setValue(SETTINGS_SECTION_WEBCLIENT, key, value);
  return true;
}

QVariantMap SettingsConnector::describeSettings()
{
  trace("describeSettings");
  return Envelope::ok(SettingsComponent::Get().settingDescriptions());
}

QVariantMap SettingsConnector::resetToDefault(const QString& section)
{
  trace("resetToDefault", section);
  if (section.isEmpty())
    SettingsComponent::Get().resetToDefaultAll();
  else
    SettingsComponent::Get().resetToDefault(section);
  return Envelope::ok();
}

PlayerConnector::PlayerConnector(QObject* parent) : ConnectorBase("player", parent)
{
  PlayerComponent& player = PlayerComponent::Get();

  connect(&player, &PlayerComponent::playing, this, &PlayerConnector::onPlaying);
  connect(&player, &PlayerComponent::finished, this, &PlayerConnector::onFinish);
  connect(&player, &PlayerComponent::canceled, this, &PlayerConnector::onFinish);
  connect(&player, &PlayerComponent::updateDuration, this, &PlayerConnector::onDurationUpdate);
  connect(&player, &PlayerComponent::positionUpdate,
          this, [this](quint64 ms) { emit onPositionUpdate(static_cast<qint64>(ms)); });

  connect(&player, &PlayerComponent::stateChanged,
          this, [this](PlayerComponent::State newState, PlayerComponent::State) {
            setBuffering(newState == PlayerComponent::State::buffering);
          });

  connect(&player, &PlayerComponent::error, this, [this](const QString& msg) {
    QLOG_ERROR() << "playback error reported to the web client as 1009:" << msg;
    emit onError(1009);
  });

  connect(&player, &PlayerComponent::videoSizeChanged, this, [this](int w, int h) {
    emit onResize(QVariantMap{ { "videoWidth", w }, { "videoHeight", h } });
  });
  connect(&player, &PlayerComponent::seekableRangesChanged,
          this, [this](const QVariant& ranges) { emit onSeekableRangesUpdate(ranges); });
  connect(&player, &PlayerComponent::audioDeviceListUpdated, this, [this]() {
    emit onAudioDeviceListUpdate(PlayerComponent::Get().getAudioDeviceList());
  });
}

void PlayerConnector::setBuffering(bool buffering)
{
  if (buffering == m_wasBuffering)
    return;

  m_wasBuffering = buffering;
  emit onBuffering(buffering);
}

QString PlayerConnector::channelLayoutToSetting(const QString& layout)
{
  if (layout == "2.1") return QStringLiteral("2.1,2.0");
  if (layout == "3.1") return QStringLiteral("3.1,2.0");
  if (layout == "5.1") return QStringLiteral("5.1,2.0");
  if (layout == "7.1") return QStringLiteral("7.1,5.1,2.0");
  if (layout == "2.0") return QStringLiteral("2.0");
  return QStringLiteral("auto");
}

QString PlayerConnector::settingToChannelLayout(const QString& setting)
{
  const QString head = setting.section(',', 0, 0).trimmed();
  if (head == "2.0" || head == "2.1" || head == "3.1" || head == "5.1" || head == "7.1")
    return head;
  return QStringLiteral("auto");
}

QStringList PlayerConnector::readableKeys() const
{
  return QStringList{ "audioDeviceList", "volume", "muted", "videoProfileList",
                      "forwardCacheSize", "audioChannelLayout", "audioDevice",
                      "audioPassthroughCodecs", "exclusiveAudioEnabled",
                      "hardwareDecodingEnabled", "transcodeToAC3Enabled",
                      "refreshRateSwitchingEnabled",
                      "refreshRateSwitchingDelayMilliseconds", "videoProfile" };
}

bool PlayerConnector::readKey(const QString& key, QVariant& out) const
{
  PlayerComponent& player = PlayerComponent::Get();

  if (key == "audioDeviceList") { out = player.getAudioDeviceList(); return true; }
  if (key == "volume")          { out = player.volume(); return true; }
  if (key == "muted")           { out = player.muted(); return true; }
  if (key == "videoProfileList")
  {
    out = QVariantList{ QVariantMap{ { "id", QStringLiteral("plex-normal-quality") },
                                     { "description", tr("Direct (mpv)") } } };
    return true;
  }
  if (key == "videoProfile")       { out = m_videoProfile; return true; }
  if (key == "audioDevice")
  {
    out = SettingsComponent::Get().value(SETTINGS_SECTION_AUDIO, "device");
    return out.isValid();
  }
  if (key == "audioChannelLayout")
  {
    out = settingToChannelLayout(
      SettingsComponent::Get().value(SETTINGS_SECTION_AUDIO, "channels").toString());
    return true;
  }
  if (key == "exclusiveAudioEnabled")
  {
    const QVariant v = SettingsComponent::Get().value(SETTINGS_SECTION_AUDIO, "exclusive");
    out = v.isValid() ? v.toBool() : false;
    return true;
  }
  if (key == "audioPassthroughCodecs" || key == "transcodeToAC3Enabled")
  {
    SettingsComponent& settings = SettingsComponent::Get();
    const QString deviceType = settings.value(SETTINGS_SECTION_AUDIO, "devicetype").toString();

    if (key == "transcodeToAC3Enabled")
    {
      out = (deviceType == "spdif");
      return true;
    }

    QVariantList codecs;
    if (deviceType != "basic")
    {
      for (const QString& codec : PlayerComponent::AudioCodecsAll())
      {
        if (settings.value(SETTINGS_SECTION_AUDIO, "passthrough." + codec).toBool())
          codecs << codec;
      }
    }
    out = codecs;
    return true;
  }
  if (key == "hardwareDecodingEnabled")
  {
    out = SettingsComponent::Get().value(SETTINGS_SECTION_VIDEO, "hardwareDecoding").toString()
          != QStringLiteral("disabled");
    return true;
  }
  if (key == "refreshRateSwitchingEnabled")
  {
    out = SettingsComponent::Get().value(SETTINGS_SECTION_VIDEO, "refreshrate.auto_switch").toBool();
    return true;
  }
  if (key == "refreshRateSwitchingDelayMilliseconds")
  {
    out = SettingsComponent::Get().value(SETTINGS_SECTION_VIDEO, "refreshrate.delay").toInt() * 1000;
    return true;
  }
  if (key == "forwardCacheSize")
  {
    out = SettingsComponent::Get().value(SETTINGS_SECTION_VIDEO, "cache");
    return out.isValid();
  }

  return ConnectorBase::readKey(key, out);
}

bool PlayerConnector::writeKey(const QString& key, const QVariant& value)
{
  PlayerComponent& player = PlayerComponent::Get();

  if (key == "volume")
  {
    const double requested = value.toDouble() * 100.0;
    if (qIsNaN(requested))
      return false;
    player.setVolume(qRound(qBound(0.0, requested, 100.0)));
    return true;
  }
  if (key == "muted")       { player.setMuted(value.toBool()); return true; }
  if (key == "audioDevice") { player.setAudioDevice(value.toString()); return true; }

  if (key == "playbackRate")  { player.setPlaybackRate(value.toDouble()); return true; }
  if (key == "subtitleDelay") { player.setSubtitleDelay(value.toLongLong()); return true; }
  if (key == "audio")         { player.setAudioStream(streamSelector(value)); return true; }
  if (key == "subtitle")      { player.setSubtitleStream(streamSelector(value)); return true; }

  if (key == "audioPassthroughCodecs")
  {
    m_passthroughCodecs.clear();
    for (const QVariant& codec : value.toList())
      m_passthroughCodecs << codec.toString();
    applyAudioPassthrough();
    return true;
  }
  if (key == "transcodeToAC3Enabled")
  {
    m_transcodeToAC3 = value.toBool();
    applyAudioPassthrough();
    return true;
  }

  if (key == "audioChannelLayout")
  {
    SettingsComponent::Get().setValue(SETTINGS_SECTION_AUDIO, "channels",
                                      channelLayoutToSetting(value.toString()));
    return true;
  }
  if (key == "exclusiveAudioEnabled")
  {
    SettingsComponent::Get().setValue(SETTINGS_SECTION_AUDIO, "exclusive", value.toBool());
    return true;
  }
  if (key == "normalizeDownmixedVolumeEnabled")
  {
    SettingsComponent::Get().setValue(SETTINGS_SECTION_AUDIO, "normalize", value.toBool());
    return true;
  }
  if (key == "hardwareDecodingEnabled")
  {
    SettingsComponent::Get().setValue(SETTINGS_SECTION_VIDEO, "hardwareDecoding",
                                      value.toBool() ? QString("enabled") : QString("disabled"));
    return true;
  }
  if (key == "refreshRateSwitchingEnabled")
  {
    SettingsComponent::Get().setValue(SETTINGS_SECTION_VIDEO, "refreshrate.auto_switch",
                                      value.toBool());
    return true;
  }
  if (key == "refreshRateSwitchingDelayMilliseconds")
  {
    SettingsComponent::Get().setValue(SETTINGS_SECTION_VIDEO, "refreshrate.delay",
                                      qRound(value.toInt() / 1000.0));
    return true;
  }
  if (key == "deinterlacingEnabled")
  {
    SettingsComponent::Get().setValue(SETTINGS_SECTION_VIDEO, "deinterlace", value.toBool());
    return true;
  }
  if (key == "subtitleDisplay")
  {
    const QVariantMap display = value.toMap();

    const QString fg = display.value("textColor").toString();
    const QString border = display.value("textBorderColor").toString();
    if (!fg.isEmpty() && !border.isEmpty())
      SettingsComponent::Get().setValue(SETTINGS_SECTION_SUBTITLES, "color", fg + "," + border);

    switch (display.value("position", -1).toInt())
    {
      case 4:
        SettingsComponent::Get().setValue(SETTINGS_SECTION_SUBTITLES, "placement", "center,top");
        break;
      case 7:
        SettingsComponent::Get().setValue(SETTINGS_SECTION_SUBTITLES, "placement", "center,center");
        break;
      case 1:
        SettingsComponent::Get().setValue(SETTINGS_SECTION_SUBTITLES, "placement", "center,bottom");
        break;
      default:
        break;
    }

    const int pct = qBound(0, display.value("size", 0).toInt(), 1000);
    if (pct > 0)
      SettingsComponent::Get().setValue(SETTINGS_SECTION_SUBTITLES, "size", 32 * pct / 100);

    return true;
  }
  if (key == "videoProfile")
  {
    const QString profile = value.toString();
    if (profile.isEmpty())
      return false;
    if (profile != m_videoProfile)
    {
      QLOG_INFO() << "videoProfile set to" << profile << "- PMP always renders through mpv";
    }
    m_videoProfile = profile;
    return true;
  }

  if (key == "forwardCacheSize")
  {
    if (value.toInt() <= 0)
      return false;

    SettingsComponent::Get().setValue(SETTINGS_SECTION_VIDEO, "cache", value.toInt());
    return true;
  }

  return ConnectorBase::writeKey(key, value);
}

void PlayerConnector::applyAudioPassthrough()
{
  SettingsComponent& settings = SettingsComponent::Get();

  QString deviceType;
  if (m_transcodeToAC3)
    deviceType = "spdif";
  else if (!m_passthroughCodecs.isEmpty())
    deviceType = "hdmi";
  else
    deviceType = "basic";

  settings.setValue(SETTINGS_SECTION_AUDIO, "devicetype", deviceType);

  QStringList enabled = m_passthroughCodecs;
  if (m_transcodeToAC3 && !enabled.contains("ac3"))
    enabled << "ac3";

  for (const QString& codec : PlayerComponent::AudioCodecsAll())
    settings.setValue(SETTINGS_SECTION_AUDIO, "passthrough." + codec, enabled.contains(codec));
}

QString PlayerConnector::streamSelector(const QVariant& stream)
{
  const QVariantMap map = stream.toMap();
  const int index = map.value("index", -1).toInt();

  if (index < 0)
    return QString();

  const QString url = map.value("url").toString();
  if (!url.isEmpty())
  {
    const QUrl parsed(url);
    if (!parsed.isValid() || (parsed.scheme() != "https" && parsed.scheme() != "http"))
    {
      QLOG_WARN() << "Refusing a stream URL that is not http or https";
      return QString();
    }
    return QString("#%1,%2").arg(index).arg(parsed.toString());
  }

  if (map.contains("ffIndex"))
  {
    const int ff = map.value("ffIndex", -1).toInt();
    if (ff >= 0)
      return QString("#%1").arg(ff);
  }

  return QString("@%1").arg(index);
}

QString PlayerConnector::itemUrl(const QVariantMap& item)
{
  if (item.contains("url"))
    return item.value("url").toString();
  return item.value("uri").toString();
}

QVariantMap PlayerConnector::open(const QVariantMap& item)
{
  trace("open");
  const QString url = itemUrl(item);
  if (url.isEmpty())
  {
    QLOG_ERROR() << "player.open with no url/uri:" << item.keys();
    return Envelope::failed();
  }

  PlayerComponent::Get().stop();
  return queue(item);
}

QVariantMap PlayerConnector::queue(const QVariantMap& item)
{
  trace("queue");
  const QString url = itemUrl(item);
  if (url.isEmpty())
  {
    QLOG_ERROR() << "player.queue with no url/uri:" << item.keys();
    return Envelope::failed();
  }

  QVariantMap options;
  const double startSeconds = item.value("startPositionSeconds", 0).toDouble();
  if (startSeconds > 0 && startSeconds < 365 * 24 * 60 * 60)
    options["startMilliseconds"] = static_cast<qint64>(startSeconds * 1000);

  QVariantMap metadata;
  const double frameRate = item.value("effectiveFrameRate", 0).toDouble();
  if (frameRate > 0)
    metadata["frameRate"] = frameRate;
  if (item.value("type").toString() == QStringLiteral("audio"))
    metadata["type"] = "music";

  const QVariantMap decision = item.value("decision").toMap();
  QVariantList streams;
  for (const char* which : { "selectedVideoStream", "selectedAudioStream" })
  {
    const QVariantMap stream = decision.value(which).toMap();
    if (!stream.isEmpty())
      streams << stream;
  }
  if (!streams.isEmpty())
  {
    QVariantMap part;
    part["Stream"] = streams;
    QVariantMap media;
    media["Part"] = QVariantList{ part };
    metadata["media"] = media;
  }

  QString audioStream;
  if (item.contains("audio"))
    audioStream = streamSelector(item.value("audio"));
  QString subtitleStream;
  if (item.contains("subtitle"))
    subtitleStream = streamSelector(item.value("subtitle"));

  PlayerComponent::Get().queueMedia(url, options, metadata, audioStream, subtitleStream);
  return Envelope::ok();
}

QVariantMap PlayerConnector::play()
{
  trace("play");
  PlayerComponent::Get().play();
  return Envelope::ok();
}

QVariantMap PlayerConnector::pause()
{
  trace("pause");
  PlayerComponent::Get().pause();
  return Envelope::ok();
}

QVariantMap PlayerConnector::stop()
{
  trace("stop");
  PlayerComponent::Get().stop();
  return Envelope::ok();
}

QVariantMap PlayerConnector::teardown()
{
  trace("teardown");
  PlayerComponent::Get().stop();
  PlayerComponent::Get().clearQueue();
  return Envelope::ok();
}

QVariantMap PlayerConnector::clearQueue()
{
  trace("clearQueue");
  PlayerComponent::Get().clearQueue();
  return Envelope::ok();
}

QVariantMap PlayerConnector::seekTo(qint64 ms)
{
  trace("seekTo");
  PlayerComponent::Get().seekTo(ms);
  return Envelope::ok();
}

QVariantMap PlayerConnector::setAudioStream(const QString& id)
{
  trace("setAudioStream");
  PlayerComponent::Get().setAudioStream(id);
  return Envelope::ok();
}

QVariantMap PlayerConnector::setSubtitleStream(const QString& id)
{
  trace("setSubtitleStream");
  PlayerComponent::Get().setSubtitleStream(id);
  return Envelope::ok();
}

QVariantMap PlayerConnector::streamSwitch()
{
  trace("streamSwitch");
  PlayerComponent::Get().streamSwitch();
  return Envelope::ok();
}

QVariantMap PlayerConnector::setVideoRectangle(int x, int y, int w, int h)
{
  trace("setVideoRectangle");
  PlayerComponent::Get().setVideoRectangle(x, y, w, h);
  return Envelope::ok();
}

QVariantMap PlayerConnector::shrinkTo(const QString& placement, int width, int height,
                                      int edgeGutterX, int edgeGutterY, bool isStackedAbove)
{
  trace("shrinkTo", placement);
  PlayerComponent::Get().setVideoPlacement(placement, width, height,
                                           edgeGutterX, edgeGutterY, isStackedAbove);
  return Envelope::ok();
}

static QString storagePath()
{
  return Paths::dataDir("webstorage.json");
}

StorageConnector::StorageConnector(QObject* parent) : ConnectorBase("storage", parent)
{
  QFile file(storagePath());
  if (!file.open(QIODevice::ReadOnly))
    return;

  QJsonParseError error;
  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject())
  {
    QLOG_WARN() << "Ignoring unreadable" << storagePath() << ":" << error.errorString();
    return;
  }

  m_data = doc.object().toVariantMap();
  QLOG_DEBUG() << "Loaded" << m_data.size() << "web storage keys";
}

static void persist(const QVariantMap& data)
{
  const QByteArray json =
    QJsonDocument(QJsonObject::fromVariantMap(data)).toJson(QJsonDocument::Compact);

  if (!Utils::safelyWriteFile(storagePath(), json))
  {
    QLOG_ERROR() << "Could not write" << storagePath();
  }
}

QVariantMap StorageConnector::itemKeys()
{
  trace("itemKeys");
  return Envelope::ok(QVariant(m_data.keys()));
}

QVariantMap StorageConnector::getItem(const QString& key)
{
  trace("getItem", key);
  return Envelope::ok(m_data.value(key));
}

QVariantMap StorageConnector::setItem(const QString& key, const QVariant& value)
{
  trace("setItem", key);
  m_data[key] = value;
  persist(m_data);
  return Envelope::ok();
}

QVariantMap StorageConnector::removeItem(const QString& key)
{
  trace("removeItem", key);
  m_data.remove(key);
  persist(m_data);
  return Envelope::ok();
}

QVariantMap StorageConnector::clear()
{
  trace("clear");
  m_data.clear();
  persist(m_data);
  return Envelope::ok();
}

InputConnector::InputConnector(QObject* parent) : ConnectorBase("input", parent)
{
  connect(&InputComponent::Get(), &InputComponent::hostInput,
          this, [this](const QStringList& actions) { emit onKeyReceived(actions); });
}

QVariantMap InputConnector::executeActions(const QVariantList& actions)
{
  trace("executeActions", actions);
  static const QStringList refusedHostCommands = { "script", "set_setting", "cycle_setting" };

  QStringList list;
  for (const QVariant& action : actions)
  {
    const QString name = action.toString();
    if (name.startsWith("host:")
        && refusedHostCommands.contains(name.mid(5).section(' ', 0, 0)))
    {
      QLOG_WARN() << "Refusing host command" << name << "from the connector";
      continue;
    }

    list << name;
  }

  InputComponent::Get().executeActions(list);
  return Envelope::ok();
}

DisplayConnector::DisplayConnector(QObject* parent) : ConnectorBase("display", parent)
{
  connect(&DisplayComponent::Get(), &DisplayComponent::refreshRateChanged, this, [this] {
    const double rate = DisplayComponent::Get().currentRefreshRate();
    emit onRefreshRateChanged(rate);
    emit onChanged("refreshRate", rate);
  });
}

QStringList DisplayConnector::readableKeys() const
{
  return QStringList{ "refreshRate" };
}

bool DisplayConnector::readKey(const QString& key, QVariant& out) const
{
  if (key == "refreshRate")
  {
    out = DisplayComponent::Get().currentRefreshRate();
    return true;
  }
  return ConnectorBase::readKey(key, out);
}

QVariantMap DisplayConnector::switchCommand(const QString& command)
{
  trace("switchCommand", command);
  DisplayComponent::Get().switchCommand(command);
  return Envelope::ok();
}

ServerConnector::ServerConnector(QObject* parent) : ConnectorBase("server", parent)
{
}

QStringList ServerConnector::readableKeys() const
{
  return QStringList{ "address", "port", "protocol", "nanoServerPort" };
}

bool ServerConnector::readKey(const QString& key, QVariant& out) const
{
  auto& settings = SettingsComponent::Get();

  if (!settings.value(SETTINGS_SECTION_MAIN, "embeddedServer").toBool())
    return false;

  if (key == "address")
  {
    out = QStringLiteral("127.0.0.1");
    return true;
  }

  if (key == "protocol")
  {
    out = QStringLiteral("http");
    return true;
  }

  if (key == "port" || key == "nanoServerPort")
  {
    out = settings.value(SETTINGS_SECTION_MAIN, "webserverport").toInt();
    return true;
  }

  return false;
}

UpdateConnector::UpdateConnector(QObject* parent) : ConnectorBase("update", parent)
{
}

QVariantMap UpdateConnector::fetch(const QString& channel)
{
  trace("fetch", channel);
  Q_UNUSED(channel);
  emit onFetchStarted();
  emit onFetchFinished(200, QVariant());
  return Envelope::ok();
}

QVariantMap UpdateConnector::download(const QVariantMap& release)
{
  trace("download");
  Q_UNUSED(release);
  return Envelope::failed();
}

QVariantMap UpdateConnector::install()
{
  trace("install");
  return Envelope::failed();
}

UserConnector::UserConnector(QObject* parent) : ConnectorBase("user", parent)
{
  connect(&SystemComponent::Get(), &SystemComponent::userInfoChanged,
          this, &UserConnector::onUserChanged);
}

QStringList UserConnector::readableKeys() const
{
  return QStringList{ "accessToken", "clientID" };
}

bool UserConnector::readKey(const QString& key, QVariant& out) const
{
  if (key == "accessToken")
  {
    out = SystemComponent::Get().authenticationToken();
    return true;
  }
  if (key == "clientID")
  {
    out = SettingsComponent::Get().value(SETTINGS_SECTION_WEBCLIENT, "clientID");
    return out.isValid();
  }

  return ConnectorBase::readKey(key, out);
}

bool UserConnector::writeKey(const QString& key, const QVariant& value)
{
  if (key == "clientID")
  {
    const QString id = value.toString();
    static const QRegularExpression allowed("\\A[^\\s\\x{0000}-\\x{001f}\\x{007f}]{1,256}\\z");
    if (!allowed.match(id).hasMatch())
    {
      QLOG_WARN() << "Refusing a client identifier that is not an identifier";
      return false;
    }

    SettingsComponent::Get().setValue(SETTINGS_SECTION_WEBCLIENT, "clientID", id);
    return true;
  }

  if (key == "accessToken")
  {
    SystemComponent::Get().setAuthenticationToken(value.toString());
    return true;
  }

  return ConnectorBase::writeKey(key, value);
}
