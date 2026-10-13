#include <locale.h>

#include <QGuiApplication>
#include <QApplication>
#include <QFileInfo>
#include <QIcon>
#include <QtQml>
#include <QQuickWindow>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#include <QErrorMessage>
#include <QCommandLineOption>

#include "shared/Names.h"
#include "system/SystemComponent.h"
#include "system/UpdateManager.h"
#include "system/UpdaterComponent.h"
#include "QsLog.h"
#include "Paths.h"
#include "player/CodecsComponent.h"
#include "player/PlayerComponent.h"
#include "player/OpenGLDetect.h"
#include "breakpad/CrashDumps.h"
#include "Version.h"
#include "settings/SettingsComponent.h"
#include "settings/SettingsSection.h"
#include "connector/ConnectorManager.h"
#include "connector/Connectors.h"
#include "ui/KonvergoWindow.h"
#include "ui/KonvergoWindow.h"
#include "Globals.h"
#include "ui/ErrorMessage.h"
#include "UniqueApplication.h"
#include "utils/HelperLauncher.h"
#include "utils/Log.h"
#include "shared/SessionEnv.h"

#include "SignalManager.h"

static void preinitPlatform()
{
#if defined(Q_OS_UNIX)
  if (qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
    return;

  if (!SessionEnv::haveWaylandDisplay())
    return;

  if (!SessionEnv::haveX11Display())
    return;

  qputenv("QT_QPA_PLATFORM", "xcb");
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////
static void preinitQt()
{
  QCoreApplication::setApplicationName(Names::MainName());
  QCoreApplication::setApplicationVersion(Version::GetVersionString());
  QCoreApplication::setOrganizationDomain("plex.tv");

  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

#ifdef Q_OS_WIN32
  QVariant useOpengl = SettingsComponent::readPreinitValue(SETTINGS_SECTION_MAIN, "useOpenGL");

  // Warning: this must be the same as the default value as declared in
  // the settings_description.json file, or confusion will result.
  if (useOpengl.typeId() != QMetaType::Bool)
    useOpengl = false;

  if (useOpengl.toBool())
    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
  else
    QCoreApplication::setAttribute(Qt::AA_UseOpenGLES);
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////
char** appendCommandLineArguments(int argc, char **argv, const QStringList& args)
{
  size_t newSize = (argc + args.length() + 1) * sizeof(char*);
  char** newArgv = (char**)calloc(1, newSize);
  memcpy(newArgv, argv, (size_t)(argc * sizeof(char*)));

  int pos = argc;
  for(const QString& str : args)
    newArgv[pos++] = qstrdup(str.toUtf8().data());

  return newArgv;
}

/////////////////////////////////////////////////////////////////////////////////////////
void ShowLicenseInfo()
{
  QFile licenses(":/misc/licenses.txt");
  licenses.open(QIODevice::ReadOnly | QIODevice::Text);
  QByteArray contents = licenses.readAll();
  printf("%.*s\n", contents.size(), contents.data());
}

/////////////////////////////////////////////////////////////////////////////////////////
static QStringList qtFlags()
{
  return { "--disable-gpu" };
}

/////////////////////////////////////////////////////////////////////////////////////////
int main(int argc, char *argv[])
{
  try
  {
    QCommandLineParser parser;
    parser.setApplicationDescription("Plex Media Player");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOptions({{{"l", "licenses"},         "Show license information"},
                       {{"a", "from-auto-update"}, "When invoked from auto-update"},
                       {"desktop",                 "Start in desktop mode"},
                       {"tv",                      "Start in TV mode"},
                       {"auto-layout",             "Use auto-layout mode"},
                       {"windowed",                "Start in windowed mode"},
                       {"fullscreen",              "Start in fullscreen"},
                       {"no-updates",              "Disable auto-updating"},
                       {"terminal",                "Log to terminal"}});

    auto scaleOption = QCommandLineOption("scale-factor", "Set to a integer or default auto which controls" \
                                                          "the scale (DPI) of the desktop interface.");
    scaleOption.setValueName("scale");
    scaleOption.setDefaultValue("auto");
    parser.addOption(scaleOption);

    const QStringList flags = qtFlags();
    char **newArgv = appendCommandLineArguments(argc, argv, flags);
    int newArgc = argc + flags.size();

    // Qt calls setlocale(LC_ALL, "") in a bunch of places, which breaks
    // float/string processing in mpv and ffmpeg.
#ifdef Q_OS_UNIX
    qputenv("LC_ALL", "C");
    qputenv("LC_NUMERIC", "C");
#endif

    preinitPlatform();
    SessionEnv::sanitizeGraphicsEnv();
    preinitQt();
    detectOpenGLEarly();

    QStringList arguments;
    for (int i = 0; i < argc; i++)
      arguments << QString::fromLatin1(argv[i]);

    if (!parser.parse(arguments) || parser.isSet("help") || parser.isSet("version"))
    {
      // This is kinda dumb. But in order for the QCommandLineParser
      // to work properly we need to init if before we call process
      // but we don't want to do that for the main application since
      // we need to set the scale factor before we do that. So it becomes
      // a small chicken-or-egg problem, which we "solve" by making
      // this temporary console app.
      //
      QCoreApplication core(newArgc, newArgv);

      // Now parse the command line.
      parser.process(arguments);
      return EXIT_SUCCESS;
    }

    if (parser.isSet("licenses"))
    {
      ShowLicenseInfo();
      return EXIT_SUCCESS;
    }

    auto scale = parser.value("scale-factor");
    if (!parser.isSet("scale-factor") && SessionEnv::insideGamescope())
    {
      scale = "1";
      qputenv("QT_SCALE_FACTOR", "1");
    }
    else if (scale == "none")
      qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");
    else if (!scale.isEmpty() && scale != "auto")
      qputenv("QT_SCALE_FACTOR", scale.toUtf8());

    // enable remote inspection if we have the correct setting for it.
    if (SettingsComponent::readPreinitValue(SETTINGS_SECTION_MAIN, "remoteInspector").toBool())
      qputenv("QTWEBENGINE_REMOTE_DEBUGGING",
              QString("127.0.0.1:%1").arg(WEB_INSPECTOR_PORT).toUtf8());

    QtWebEngineQuick::initialize();

    QApplication app(newArgc, newArgv);
    app.setWindowIcon(QIcon(":/images/icon.png"));

    // init breakpad.
    setupCrashDumper();

    UniqueApplication* uniqueApp = new UniqueApplication();
    if (!uniqueApp->ensureUnique())
      return EXIT_SUCCESS;

#ifdef Q_OS_UNIX
    // install signals handlers for proper app closing.
    SignalManager signalManager(&app);
    Q_UNUSED(signalManager);
#endif

    Log::Init();
    if (parser.isSet("terminal"))
      Log::EnableTerminalOutput();

    QLOG_INFO() << "Session:" << qPrintable(SessionEnv::description());
    QLOG_INFO() << "Qt platform plugin:" << qPrintable(app.platformName())
                << "scale-factor:" << qPrintable(scale);
    if (!SessionEnv::graphicsEnvReport().isEmpty())
      QLOG_INFO() << "Graphics env:" << qPrintable(SessionEnv::graphicsEnvReport());

    // Quit app and apply update if we find one.
    if (!parser.isSet("no-updates") && !SessionEnv::insideFlatpak() && UpdateManager::CheckForUpdates())
    {
      app.quit();
      return 0;
    }

    detectOpenGLLate();

    Codecs::preinitCodecs();

    // Initialize all the components. This needs to be done
    // early since most everything else relies on it
    //
    ComponentManager::Get().initialize();

    ConnectorManager::Get().initialize();

    if (parser.isSet("no-updates"))
      UpdaterComponent::Get().disable();

    SettingsComponent::Get().setCommandLineValues(parser.optionNames());

    // start our helper
#if ENABLE_HELPER
    HelperLauncher::Get().connectToHelper();
#endif
    // load QtWebChannel so that we can register our components with it.
    QQmlApplicationEngine *engine = Globals::Engine();

    KonvergoWindow::RegisterClass();
    Globals::SetContextProperty("components", &ComponentManager::Get().getQmlPropertyMap());

    // the only way to detect if QML parsing fails is to hook to this signal and then see
    // if we get a valid object passed to it. Any error messages will be reported on stderr
    // but since no normal user should ever see this it should be fine
    //
    QObject::connect(engine, &QQmlApplicationEngine::objectCreated, [=](QObject* object, const QUrl& url)
    {
      Q_UNUSED(url);

      if (object == nullptr)
        throw FatalException(QObject::tr("Failed to parse application engine script."));

      KonvergoWindow* window = Globals::MainWindow();

      QObject* webChannel = qvariant_cast<QObject*>(window->property("webChannel"));
      Q_ASSERT(webChannel);
      ComponentManager::Get().setWebChannel(qobject_cast<QWebChannel*>(webChannel));
      ConnectorManager::Get().setWebChannel(qobject_cast<QWebChannel*>(webChannel));
      ConnectorManager::Get().trackWindow(window);

      QObject::connect(uniqueApp, &UniqueApplication::otherApplicationStarted, window, &KonvergoWindow::otherAppFocus);
    });
    engine->load(QUrl(QStringLiteral("qrc:/ui/webview.qml")));

    Log::UpdateLogLevel();

    // run our application
    int ret = app.exec();

    delete uniqueApp;
    Globals::EngineDestroy();

    Codecs::Uninit();
    Log::Uninit();
    return ret;
  }
  catch (FatalException& e)
  {
    QLOG_FATAL() << "Unhandled FatalException:" << qPrintable(e.message());
    QApplication errApp(argc, argv);

    auto  msg = new ErrorMessage(e.message(), true);
    msg->show();

    errApp.exec();

    Codecs::Uninit();
    Log::Uninit();
    return 1;
  }
}
