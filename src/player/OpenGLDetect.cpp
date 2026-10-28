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

static const int kEglAttemptLimit = 2;

struct GLState
{
  QString version;
  QString integration;
  QString stall;
  int strikes = 0;
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
    else if (key == QLatin1String("strikes"))
      state.strikes = value.toInt();
  }

  return state;
}

static void writeState(const QString& integration, int strikes)
{
  QFile file(statePath());
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return;

  const QString text = QStringLiteral("version=%1\nintegration=%2\nstrikes=%3\n")
                         .arg(Version::GetVersionString(), integration)
                         .arg(strikes);
  file.write(text.toLatin1());
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
    writeState(QStringLiteral("xcb_egl"), g_strikes);
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

  if (sameBuild)
  {
    g_strikes = state.strikes;
    if (state.stall == QLatin1String("frame"))
    {
      if (state.integration == QLatin1String("xcb_egl"))
        g_strikes++;
      else
        g_strikes = 0;
    }
  }

  if (g_strikes >= kEglAttemptLimit)
  {
    writeState(QStringLiteral("glx"), g_strikes);
    g_openGLReport = QStringLiteral("xcb_egl is off for this build (%1 starts with it presented "
                                    "no frame); GLX instead, so no 3D")
                       .arg(g_strikes);
    return;
  }

  useEgl(g_strikes + 1);
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
  writeState(QStringLiteral("xcb_egl"), 0);
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
