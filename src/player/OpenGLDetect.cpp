#include <QtGlobal>
#include <QFile>
#include <QSurfaceFormat>
#include <QCoreApplication>
#include <QOpenGLContext>

#include <mpv/client.h>

#include "QsLog.h"

#include "QtHelper.h"
#include "OpenGLDetect.h"
#include "shared/Paths.h"
#include "shared/SessionEnv.h"

static QString g_openGLReport;
static bool g_eglAttempted = false;

static const int kEglAttemptLimit = 2;

static QString markerPath()
{
  return Paths::dataDir("xcb-egl-state");
}

static QString readMarker()
{
  QFile file(markerPath());
  if (!file.open(QIODevice::ReadOnly))
    return QString();
  const QString state = QString::fromLatin1(file.readAll()).trimmed();
  file.close();
  return state;
}

static void writeMarker(const QString& state)
{
  QFile file(markerPath());
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return;
  file.write(state.toLatin1());
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
    writeMarker(QStringLiteral("attempt:%1").arg(attempt));
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
  const QString state = readMarker();

  if (state.startsWith(QLatin1String("off")))
  {
    g_openGLReport = QStringLiteral("xcb_egl is off (it failed %1 times; GLX instead)")
                       .arg(kEglAttemptLimit);
    return;
  }

  if (state.startsWith(QLatin1String("attempt:")))
  {
    const int failed = state.mid(8).toInt();
    if (failed >= kEglAttemptLimit)
    {
      writeMarker(QStringLiteral("off"));
      g_openGLReport = QStringLiteral("xcb_egl turned off (%1 starts with it presented no frame)")
                         .arg(failed);
    }
    else
    {
      writeMarker(QStringLiteral("failed:%1").arg(failed));
      g_openGLReport = QStringLiteral("xcb_egl held back after attempt %1 presented no frame "
                                      "(GLX now, retrying next start)").arg(failed);
    }
    return;
  }

  if (state.startsWith(QLatin1String("failed:")))
  {
    useEgl(state.mid(7).toInt() + 1);
    return;
  }

  useEgl(1);
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
  if (QFile::exists(markerPath()))
    QFile::remove(markerPath());
}

///////////////////////////////////////////////////////////////////////////////////////////////////
QString openGLReport()
{
  return g_openGLReport;
}
