#include <QtGlobal>
#include <QFile>
#include <QSurfaceFormat>
#include <QCoreApplication>
#include <QOpenGLContext>

#include <mpv/client.h>

#include "QsLog.h"

#include "QtHelper.h"
#include "OpenGLDetect.h"
#include "core/Version.h"
#include "shared/Paths.h"
#include "shared/SessionEnv.h"

static QString g_openGLReport;
static bool g_eglAttempted = false;
static int g_strikes = 0;
static QString g_lastEnding;

static const int kEglAttemptLimit = 2;

struct GLState
{
  QString version;
  QString integration;
  QString stall;
  QString crash;
  int strikes = 0;
  bool pending = false;
};

static QString statePath()
{
  return Paths::dataDir("gl-state");
}

static GLState readState()
{
  GLState state;

  QFile file(statePath());
  if (!file.open(QIODevice::ReadOnly))
    return state;

  const QStringList lines = QString::fromLatin1(file.readAll()).split(QLatin1Char('\n'));
  file.close();

  for (const QString& line : lines)
  {
    const int split = line.indexOf(QLatin1Char('='));
    if (split < 1)
      continue;

    const QString key = line.left(split).trimmed();
    const QString value = line.mid(split + 1).trimmed();

    if (key == QLatin1String("version"))
      state.version = value;
    else if (key == QLatin1String("integration"))
      state.integration = value;
    else if (key == QLatin1String("stall"))
      state.stall = value;
    else if (key == QLatin1String("crash"))
      state.crash = value;
    else if (key == QLatin1String("strikes"))
      state.strikes = value.toInt();
    else if (key == QLatin1String("pending"))
      state.pending = value.toInt() != 0;
  }

  return state;
}

static void writeState(const QString& integration, int strikes, bool pending,
                       const QString& last = QString())
{
  QFile file(statePath());
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return;

  QString text = QStringLiteral("version=%1\nintegration=%2\nstrikes=%3\npending=%4\n")
                   .arg(Version::GetVersionString(), integration)
                   .arg(strikes)
                   .arg(pending ? 1 : 0);
  if (!last.isEmpty())
    text += QStringLiteral("last=%1\n").arg(last);

  file.write(text.toLatin1());
  file.flush();
  file.close();
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// Attempt to reuse mpv's code for detecting whether we want GLX or EGL (which
// is tricky to do because of hardware decoding concerns). This is not pretty,
// but quite effective and without having to duplicate too much GLX/EGL code.
static QString probeHwdecInterop()
{
  auto mpv = mpv::qt::Handle::FromRawHandle(mpv_create());
  if (!mpv)
    return "";
  mpv::qt::set_property(mpv, "gpu-hwdec-interop", "all");
  // Actually creating a window is required. There is currently no way to keep
  // this window hidden or invisible.
  mpv::qt::set_property(mpv, "force-window", true);
  // As a mitigation, put the window in the top/right corner, and make it as
  // small as possible by forcing 1x1 size and removing window borders.
  mpv::qt::set_property(mpv, "geometry", "1x1+0+0");
  mpv::qt::set_property(mpv, "border", false);
  if (mpv_initialize(mpv) < 0)
    return "";
  return mpv::qt::get_property(mpv, "hwdec-interop").toString();
}

static bool shouldProbeHwdecInterop()
{
  if (qEnvironmentVariableIsSet("QT_XCB_GL_INTEGRATION"))
    return false;

  const QByteArray override = qgetenv("PMP_HWDEC_PROBE");
  if (!override.isEmpty())
    return override != "0";

  if (!SessionEnv::haveX11Display())
    return false;

  if (SessionEnv::insideGamescope())
    return false;

#if defined(Q_PROCESSOR_ARM) || defined(Q_PROCESSOR_ARM_64)
  return false;
#else
  return true;
#endif
}

static QString previousFatal()
{
  QFile file(Paths::logDir("stderr.log"));
  if (!file.open(QIODevice::ReadOnly))
    return QString();

  const qint64 size = file.size();
  const qint64 window = 64 * 1024;
  if (size > window)
    file.seek(size - window);

  const QStringList lines = QString::fromLatin1(file.readAll()).split(QLatin1Char('\n'));
  file.close();

  for (int at = lines.size() - 1; at >= 0; at--)
  {
    const QString line = lines.at(at);
    if (!line.contains(QLatin1String(":FATAL:")) && !line.contains(QLatin1String("Check failed")))
      continue;

    const int mark = line.indexOf(QLatin1String(":FATAL:"));
    QString text = mark < 0 ? line : line.mid(mark + 7);
    text = text.simplified();
    if (text.size() > 160)
      text = text.left(160);
    return text;
  }

  return QString();
}

static QString previousEnding(const GLState& state, const QString& fatal)
{
  QStringList parts;
  if (!state.crash.isEmpty())
    parts << QStringLiteral("crash=%1").arg(state.crash);
  if (!state.stall.isEmpty())
    parts << QStringLiteral("stall=%1").arg(state.stall);
  if (!fatal.isEmpty())
    parts << QStringLiteral("fatal=%1").arg(fatal);
  return parts.join(QLatin1Char(' '));
}

enum class EglChoice
{
  No,
  Yes,
  Auto
};

static EglChoice eglChoice()
{
  if (qEnvironmentVariableIsSet("QT_XCB_GL_INTEGRATION"))
    return EglChoice::No;

  if (!SessionEnv::haveX11Display())
    return EglChoice::No;

  const QByteArray override = qgetenv("PMP_XCB_EGL");
  if (!override.isEmpty())
    return override == "0" ? EglChoice::No : EglChoice::Yes;

  return SessionEnv::insideGamescope() ? EglChoice::Auto : EglChoice::No;
}

static void useEgl(int attempt)
{
  g_eglAttempted = true;
  qputenv("QT_XCB_GL_INTEGRATION", "xcb_egl");
  if (attempt > 0)
  {
    writeState(QStringLiteral("xcb_egl"), g_strikes, true, g_lastEnding);
    g_openGLReport = QStringLiteral("QT_XCB_GL_INTEGRATION=xcb_egl (X11 under gamescope; GLX "
                                    "offers no EGL display; attempt %1 of %2)")
                       .arg(attempt).arg(kEglAttemptLimit);
  }
  else
  {
    g_openGLReport = "QT_XCB_GL_INTEGRATION=xcb_egl (forced by PMP_XCB_EGL)";
  }
}

static void chooseEglAutomatically()
{
  const GLState state = readState();
  const bool sameBuild = state.version == Version::GetVersionString();
  const QString fatal = previousFatal();

  g_lastEnding = previousEnding(state, fatal);

  if (sameBuild)
  {
    g_strikes = state.strikes;

    const bool neverPresented = state.pending || !state.stall.isEmpty();
    const bool diedElsewhere = !state.crash.isEmpty() || !fatal.isEmpty();

    if (neverPresented && !diedElsewhere && state.integration == QLatin1String("xcb_egl"))
      g_strikes++;
  }

  if (g_strikes >= kEglAttemptLimit)
  {
    writeState(QStringLiteral("glx"), g_strikes, false, g_lastEnding);
    g_openGLReport = QStringLiteral("xcb_egl is off for this build (%1 starts with it presented "
                                    "no frame); GLX instead, so no 3D")
                       .arg(g_strikes);
    return;
  }

  useEgl(g_strikes + 1);

  if (!g_lastEnding.isEmpty())
    g_openGLReport += QStringLiteral("; the previous start ended with %1").arg(g_lastEnding);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void detectOpenGLEarly()
{
  const EglChoice choice = eglChoice();

  if (choice == EglChoice::Yes)
  {
    useEgl(0);
    return;
  }

  if (choice == EglChoice::Auto)
  {
    chooseEglAutomatically();
    return;
  }

  if (!shouldProbeHwdecInterop())
    return;

  // The putenv call must happen before Qt initializes its platform stuff.
  if (probeHwdecInterop() == "vaapi-egl")
  {
    qputenv("QT_XCB_GL_INTEGRATION", "xcb_egl");
    g_openGLReport = "QT_XCB_GL_INTEGRATION=xcb_egl (mpv reports vaapi-egl interop)";
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void detectOpenGLLate()
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void confirmOpenGLSession()
{
  if (!g_eglAttempted)
    return;

  g_strikes = 0;
  writeState(QStringLiteral("xcb_egl"), 0, false, g_lastEnding);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
QString openGLStatePath()
{
  return statePath();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
QString openGLReport()
{
  return g_openGLReport;
}
