#ifndef __CONNECTORS_H__
#define __CONNECTORS_H__

#include "ConnectorBase.h"

#include <QHash>
#include <QPointer>
#include <QWindow>

static const int WEB_INSPECTOR_PORT = 9992;

class SystemConnector : public ConnectorBase
{
  Q_OBJECT

public:
  explicit SystemConnector(QObject* parent = nullptr);

  Q_INVOKABLE QVariantMap describe();
  Q_INVOKABLE QVariantMap getDescription() { return describe(); }

  Q_INVOKABLE QVariantMap ready(bool isReady);
  Q_INVOKABLE QVariantMap isReady();
  Q_INVOKABLE QVariantMap exit();
  Q_INVOKABLE QVariantMap restart();
  Q_INVOKABLE QVariantMap toggleFullscreen();
  Q_INVOKABLE QVariantMap openExternalUrl(const QString& url);
  Q_INVOKABLE QVariantMap setCursorVisibility(bool visible);
  Q_INVOKABLE QVariantMap userInformation(const QVariantMap& userModel);

  void trackWindow(QWindow* window);

  int beginDeferred();
  void completeDeferred(int index, const QVariantMap& envelope);

signals:
  void onDeferredReturnValueAvailable(int index, const QVariant& value);
  void onVisibilityChanged(int visibility);
  void onVisibilityChangedByUser(int visibility);

protected:
  bool readKey(const QString& key, QVariant& out) const override;
  bool writeKey(const QString& key, const QVariant& value) override;
  QStringList readableKeys() const override;

private:
  bool m_ready = false;
  int m_deferIndex = 0;
  QPointer<QWindow> m_window;
};

class SettingsConnector : public ConnectorBase
{
  Q_OBJECT

public:
  explicit SettingsConnector(QObject* parent = nullptr);

  Q_INVOKABLE QVariantMap describeSettings();
  Q_INVOKABLE QVariantMap resetToDefault(const QString& section);

signals:
  void onChanged(const QString& key, const QVariant& value);
  void onSettingsDescriptionChanged();

protected:
  bool readKey(const QString& key, QVariant& out) const override;
  bool writeKey(const QString& key, const QVariant& value) override;
  QStringList readableKeys() const override;

private slots:
  void onSectionValuesUpdated(const QString& section, const QVariantMap& values);

private:
  static bool split(const QString& key, QString& section, QString& leaf);
};

class PlayerConnector : public ConnectorBase
{
  Q_OBJECT

public:
  explicit PlayerConnector(QObject* parent = nullptr);

  Q_INVOKABLE QVariantMap open(const QVariantMap& item);
  Q_INVOKABLE QVariantMap queue(const QVariantMap& item);
  Q_INVOKABLE QVariantMap play();
  Q_INVOKABLE QVariantMap pause();
  Q_INVOKABLE QVariantMap stop();
  Q_INVOKABLE QVariantMap teardown();
  Q_INVOKABLE QVariantMap clearQueue();
  Q_INVOKABLE QVariantMap seekTo(qint64 ms);
  Q_INVOKABLE QVariantMap setAudioStream(const QString& id);
  Q_INVOKABLE QVariantMap setSubtitleStream(const QString& id);
  Q_INVOKABLE QVariantMap streamSwitch();
  Q_INVOKABLE QVariantMap setVideoRectangle(int x, int y, int w, int h);

  Q_INVOKABLE QVariantMap shrinkTo(const QString& placement, int width, int height,
                                   int edgeGutterX, int edgeGutterY, bool isStackedAbove);

signals:
  void onAudioDeviceListUpdate(const QVariant& devices);
  void onPlaying();
  void onFinish();
  void onError(int code);
  void onDurationUpdate(qint64 ms);
  void onPositionUpdate(qint64 ms);
  void onBuffering(bool isBuffering);
  void onSeekableRangesUpdate(const QVariant& ranges);
  void onResize(const QVariant& size);
  void onVideoCoverClicked();

protected:
  bool readKey(const QString& key, QVariant& out) const override;
  bool writeKey(const QString& key, const QVariant& value) override;
  QStringList readableKeys() const override;

private:
  void setBuffering(bool buffering);

  static QString itemUrl(const QVariantMap& item);

  static QString streamSelector(const QVariant& stream);

  void applyAudioPassthrough();

  static QString channelLayoutToSetting(const QString& layout);
  static QString settingToChannelLayout(const QString& setting);

  QStringList m_passthroughCodecs;
  bool m_transcodeToAC3 = false;
  bool m_wasBuffering = false;

  QString m_videoProfile = QStringLiteral("plex-normal-quality");
};

class StorageConnector : public ConnectorBase
{
  Q_OBJECT

public:
  explicit StorageConnector(QObject* parent = nullptr);

  Q_INVOKABLE QVariantMap itemKeys();
  Q_INVOKABLE QVariantMap getItem(const QString& key);
  Q_INVOKABLE QVariantMap setItem(const QString& key, const QVariant& value);
  Q_INVOKABLE QVariantMap removeItem(const QString& key);
  Q_INVOKABLE QVariantMap clear();
};

class InputConnector : public ConnectorBase
{
  Q_OBJECT

public:
  explicit InputConnector(QObject* parent = nullptr);

  Q_INVOKABLE QVariantMap executeActions(const QVariantList& actions);

signals:
  void onKeyReceived(const QVariant& keys);
  void onCommandReceived(const QString& command, const QVariant& arguments);
};

class DisplayConnector : public ConnectorBase
{
  Q_OBJECT

public:
  explicit DisplayConnector(QObject* parent = nullptr);

  Q_INVOKABLE QVariantMap switchCommand(const QString& command);

signals:
  void onRefreshRateChanged(double refreshRate);

protected:
  bool readKey(const QString& key, QVariant& out) const override;
  QStringList readableKeys() const override;
};

class ServerConnector : public ConnectorBase
{
  Q_OBJECT

public:
  explicit ServerConnector(QObject* parent = nullptr);

signals:
  void onNanoServerPortChanged(int newPort);

protected:
  bool readKey(const QString& key, QVariant& out) const override;
  QStringList readableKeys() const override;
};

class UpdateConnector : public ConnectorBase
{
  Q_OBJECT

public:
  explicit UpdateConnector(QObject* parent = nullptr);

  Q_INVOKABLE QVariantMap fetch(const QString& channel);
  Q_INVOKABLE QVariantMap download(const QVariantMap& release);
  Q_INVOKABLE QVariantMap install();

signals:
  void onFetchStarted();
  void onFetchFinished(int responseCode, const QVariant& response);
  void onDownloadStarted();
  void onDownloadProgress(int downloadIndex, int downloadsTotal, qint64 bytesReceived,
                          qint64 bytesTotal);
  void onDownloadFinished();
};

class UserConnector : public ConnectorBase
{
  Q_OBJECT

public:
  explicit UserConnector(QObject* parent = nullptr);

signals:
  void onUserChanged();

protected:
  bool readKey(const QString& key, QVariant& out) const override;
  bool writeKey(const QString& key, const QVariant& value) override;
  QStringList readableKeys() const override;
};

#endif
