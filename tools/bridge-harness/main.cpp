
#include <QtCore/QCommandLineParser>
#include <QtCore/QDebug>
#include <QtCore/QJsonDocument>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtWebChannel/QWebChannel>
#include <QtWebEngineWidgets/QWebEnginePage>
#include <QtWebEngineWidgets/QWebEngineProfile>
#include <QtWebEngineWidgets/QWebEngineSettings>
#include <QtWebEngineWidgets/QWebEngineView>
#include <QtWidgets/QApplication>

#include <cstdio>

static const int kOk = 0;
static const int kDeferred = 3;
static const int kFailed = 1;

static QVariantMap ok(const QVariant& result = QVariant())
{
  QVariantMap m;
  m["errorCode"] = kOk;
  m["result"] = result;
  return m;
}

static QVariantMap failed(int code = kFailed)
{
  QVariantMap m;
  m["errorCode"] = code;
  return m;
}

static QVariantMap deferred(int index)
{
  QVariantMap inner;
  inner["index"] = index;
  QVariantMap d;
  d["deferred"] = inner;
  QVariantMap m;
  m["errorCode"] = kDeferred;
  m["result"] = d;
  return m;
}

static QString brief(const QVariant& v)
{
  return QString::fromUtf8(QJsonDocument::fromVariant(v).toJson(QJsonDocument::Compact));
}

static void trace(const QString& what, const QVariant& detail = QVariant())
{
  const QString line = detail.isValid() ? QString("[bridge] %1 %2").arg(what, brief(detail))
                                        : QString("[bridge] %1").arg(what);
  std::fputs(qPrintable(line + "\n"), stdout);
  std::fflush(stdout);
}

class Connector : public QObject
{
  Q_OBJECT

public:
  Connector(const QString& name, QObject* parent = nullptr) : QObject(parent), m_name(name) {}

  Q_INVOKABLE QVariantMap getAll()
  {
    trace(m_name + ".getAll");
    QVariantMap per;
    for (auto it = m_data.constBegin(); it != m_data.constEnd(); ++it)
      per[it.key()] = ok(it.value());
    return ok(per);
  }

  Q_INVOKABLE QVariantMap get(const QVariantList& keys)
  {
    trace(m_name + ".get", keys);
    QVariantMap per;
    for (const QVariant& k : keys)
    {
      const QString key = k.toString();
      per[key] = m_data.contains(key) ? ok(m_data.value(key)) : failed();
    }
    return ok(per);
  }

  Q_INVOKABLE QVariantMap set(const QVariantMap& values)
  {
    trace(m_name + ".set", values);
    QVariantMap per;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
    {
      m_data[it.key()] = it.value();
      per[it.key()] = ok();
      emit onChanged(it.key(), it.value());
    }
    return ok(per);
  }

  Q_INVOKABLE QVariantMap keys()
  {
    trace(m_name + ".keys");
    return ok(QVariant(QStringList(m_data.keys())));
  }

  Q_INVOKABLE QVariantMap data()
  {
    trace(m_name + ".data");
    return ok(m_data);
  }

signals:
  void onChanged(const QString& key, const QVariant& value);

protected:
  QString m_name;
  QVariantMap m_data;
};

class SystemConnector : public Connector
{
  Q_OBJECT

public:
  explicit SystemConnector(QObject* parent = nullptr) : Connector("system", parent)
  {
    m_data["visibility"] = 2;
    m_data["sleepEnabled"] = true;
  }

  Q_INVOKABLE QVariantMap describe()
  {
    trace("system.describe");
    QVariantMap d;
    d["product"] = "Plex Media Player";
    d["version"] = "2.58.0-arm64-revival";
    d["platform"] = "Linux";
    d["deviceName"] = "bridge-harness";
    return ok(d);
  }

  Q_INVOKABLE QVariantMap getDescription() { return describe(); }

  Q_INVOKABLE QVariantMap ready(bool isReady)
  {
    trace("system.ready", isReady);
    m_ready = isReady;
    return ok();
  }

  Q_INVOKABLE QVariantMap isReady() { return ok(m_ready); }
  Q_INVOKABLE QVariantMap toggleFullscreen() { trace("system.toggleFullscreen"); return ok(); }
  Q_INVOKABLE QVariantMap exit() { trace("system.exit"); return ok(); }

  Q_INVOKABLE QVariantMap deferredProbe()
  {
    const int index = ++m_deferIndex;
    trace("system.deferredProbe -> deferred", index);
    QTimer::singleShot(50, this, [this, index] {
      trace("system.onDeferredReturnValueAvailable", index);
      emit onDeferredReturnValueAvailable(index, ok(true));
    });
    return deferred(index);
  }

signals:
  void onDeferredReturnValueAvailable(int index, const QVariant& value);
  void onVisibilityChanged(int visibility);
  void onVisibilityChangedByUser(int visibility);

private:
  bool m_ready = false;
  int m_deferIndex = 0;
};

class ServerConnector : public Connector
{
  Q_OBJECT

public:
  explicit ServerConnector(QObject* parent = nullptr) : Connector("server", parent) {}

  Q_INVOKABLE QVariantMap get(const QVariantList& keys)
  {
    trace("server.get -> FAILING DELIBERATELY", keys);
    return failed();
  }

signals:
  void onNanoServerPortChanged(int newPort);
};

class StorageConnector : public Connector
{
  Q_OBJECT

public:
  explicit StorageConnector(QObject* parent = nullptr) : Connector("storage", parent) {}

  Q_INVOKABLE QVariantMap itemKeys() { return ok(QVariant(QStringList(m_data.keys()))); }

  Q_INVOKABLE QVariantMap getItem(const QString& key)
  {
    trace("storage.getItem", key);
    return m_data.contains(key) ? ok(m_data.value(key)) : ok();
  }

  Q_INVOKABLE QVariantMap setItem(const QString& key, const QVariant& value)
  {
    trace("storage.setItem", key);
    m_data[key] = value;
    return ok();
  }

  Q_INVOKABLE QVariantMap removeItem(const QString& key)
  {
    trace("storage.removeItem", key);
    m_data.remove(key);
    return ok();
  }

  Q_INVOKABLE QVariantMap clear()
  {
    trace("storage.clear");
    m_data.clear();
    return ok();
  }
};

class PlayerConnector : public Connector
{
  Q_OBJECT

public:
  explicit PlayerConnector(QObject* parent = nullptr) : Connector("player", parent)
  {
    m_data["audioDeviceList"] = QVariantList{ QVariantMap{ { "name", "auto" }, { "description", "Autoselect device" } } };
    m_data["audioDevice"] = "auto";
    m_data["forwardCacheSize"] = 75;
    m_data["videoProfileList"] = QVariantList{ "mpv" };
  }

  Q_INVOKABLE QVariantMap play() { trace("player.play"); return ok(); }
  Q_INVOKABLE QVariantMap stop() { trace("player.stop"); return ok(); }
  Q_INVOKABLE QVariantMap teardown() { trace("player.teardown"); return ok(); }
  Q_INVOKABLE QVariantMap seekTo(qint64 ms) { trace("player.seekTo", ms); return ok(); }

  Q_INVOKABLE QVariantMap queue(const QVariant& item)
  {
    trace("player.queue", item);
    return ok();
  }

  Q_INVOKABLE QVariantMap shrinkTo(const QVariant& a, const QVariant& b, const QVariant& c,
                                   const QVariant& d, const QVariant& e, const QVariant& f)
  {
    trace("player.shrinkTo", QVariantList{ a, b, c, d, e, f });
    return ok();
  }

signals:
  void onAudioDeviceListUpdate(const QVariant& devices);
};

class SettingsConnector : public Connector
{
  Q_OBJECT
public:
  explicit SettingsConnector(QObject* parent = nullptr) : Connector("settings", parent) {}
signals:
  void onSettingChanged(const QString& key, const QVariant& value);
};

class InputConnector : public Connector
{
  Q_OBJECT
public:
  explicit InputConnector(QObject* parent = nullptr) : Connector("input", parent) {}
signals:
  void onKeyReceived(const QVariant& keys);
};

class DisplayConnector : public Connector
{
  Q_OBJECT
public:
  explicit DisplayConnector(QObject* parent = nullptr) : Connector("display", parent)
  {
    m_data["refreshRate"] = 60.0;
  }
signals:
  void refreshRateChanged(double refreshRate);
};

class UserConnector : public Connector
{
  Q_OBJECT
public:
  explicit UserConnector(QObject* parent = nullptr) : Connector("user", parent) {}
};

class UpdateConnector : public Connector
{
  Q_OBJECT
public:
  explicit UpdateConnector(QObject* parent = nullptr) : Connector("update", parent) {}

  Q_INVOKABLE QVariantMap fetch(const QString& channel)
  {
    trace("update.fetch", channel);
    emit onFetchStarted();
    emit onFetchFinished(200, QVariant());
    return ok();
  }

  Q_INVOKABLE QVariantMap download(const QVariant& release)
  {
    trace("update.download", release);
    return ok();
  }

  Q_INVOKABLE QVariantMap install() { trace("update.install"); return ok(); }

signals:
  void onFetchStarted();
  void onFetchFinished(int responseCode, const QVariant& response);
  void onDownloadStarted();
  void onDownloadProgress(int downloadIndex, int downloadsTotal, qint64 bytesReceived, qint64 bytesTotal);
  void onDownloadFinished();
};

class TracingPage : public QWebEnginePage
{
  Q_OBJECT

public:
  explicit TracingPage(QWebEngineProfile* profile, QObject* parent = nullptr)
    : QWebEnginePage(profile, parent)
  {
  }

protected:
  void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level, const QString& message,
                                int lineNumber, const QString& sourceID) override
  {
    const char* tag = level == InfoMessageLevel      ? "info"
                      : level == WarningMessageLevel ? "warn"
                                                     : "error";
    Q_UNUSED(lineNumber);
    Q_UNUSED(sourceID);
    std::fputs(qPrintable(QString("[js:%1] %2\n").arg(tag, message)), stdout);
    std::fflush(stdout);
  }

  bool certificateError(const QWebEngineCertificateError& error) override
  {
    Q_UNUSED(error);
    return true;
  }
};

int main(int argc, char* argv[])
{
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QApplication app(argc, argv);
  QApplication::setApplicationName("plex-bridge-harness");

  QCommandLineParser parser;
  parser.setApplicationDescription("Probe the modern Plex Web bridge protocol.");
  parser.addHelpOption();
  parser.addPositionalArgument("url", "URL of the web client to load.");
  QCommandLineOption quitAfter(QStringLiteral("quit-after"), "Exit after N seconds.", "seconds", "45");
  parser.addOption(quitAfter);
  QCommandLineOption dumpDom(QStringLiteral("dump-dom"), "Dump the rendered DOM after N seconds.", "seconds");
  parser.addOption(dumpDom);
  parser.process(app);

  const QStringList args = parser.positionalArguments();
  if (args.isEmpty())
  {
    std::fputs("error: no URL given\n", stderr);
    return 2;
  }

  QWebEngineProfile* profile = new QWebEngineProfile("harness", &app);
  profile->settings()->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
  profile->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
  profile->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
  profile->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);

  QWebChannel* channel = new QWebChannel(&app);
  SystemConnector* system = new SystemConnector(&app);
  channel->registerObject("system", system);
  channel->registerObject("server", new ServerConnector(&app));
  channel->registerObject("storage", new StorageConnector(&app));
  channel->registerObject("player", new PlayerConnector(&app));
  channel->registerObject("settings", new SettingsConnector(&app));
  channel->registerObject("input", new InputConnector(&app));
  channel->registerObject("display", new DisplayConnector(&app));
  channel->registerObject("user", new UserConnector(&app));
  channel->registerObject("update", new UpdateConnector(&app));

  TracingPage* page = new TracingPage(profile, &app);
  page->setWebChannel(channel);

  QWebEngineView* view = new QWebEngineView;
  view->setPage(page);
  view->resize(1280, 800);

  QObject::connect(page, &QWebEnginePage::loadFinished, [](bool okLoad) {
    trace(QString("loadFinished ok=%1").arg(okLoad ? "true" : "false"));
  });

  trace("loading", args.first());
  page->load(QUrl(args.first()));
  view->show();

  if (parser.isSet(dumpDom))
  {
    const int at = parser.value(dumpDom).toInt();
    QTimer::singleShot(at * 1000, page, [page] {
      page->runJavaScript(QStringLiteral(
        "JSON.stringify({"
        "  url: location.href,"
        "  hash: location.hash,"
        "  title: document.title,"
        "  bodyClass: document.body.className,"
        "  text: document.body.innerText.slice(0, 600),"
        "  topLevel: Array.from(document.body.children).map("
        "    e => e.tagName + '#' + e.id + '.' + e.className).slice(0, 20),"
        "  localStorageKeys: Object.keys(window.localStorage || {})"
        "}, null, 1)"), [](const QVariant& v) {
        std::fputs(qPrintable(QString("[bridge] dom %1\n").arg(v.toString())), stdout);
        std::fflush(stdout);
      });
    });
  }

  const int seconds = parser.value(quitAfter).toInt();
  if (seconds > 0)
    QTimer::singleShot(seconds * 1000, &app, [] {
      trace("quit-after reached");
      QCoreApplication::quit();
    });

  return app.exec();
}

#include "main.moc"
