#include <QtGlobal>
#include <QSurfaceFormat>
#include <QCoreApplication>
#include <QOpenGLContext>

#include <mpv/client.h>

#include "QsLog.h"

#include "QtHelper.h"
#include "OpenGLDetect.h"
#include "shared/SessionEnv.h"


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

///////////////////////////////////////////////////////////////////////////////////////////////////
void detectOpenGLEarly()
{
  if (!shouldProbeHwdecInterop())
    return;

  // The putenv call must happen before Qt initializes its platform stuff.
  if (probeHwdecInterop() == "vaapi-egl")
    qputenv("QT_XCB_GL_INTEGRATION", "xcb_egl");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void detectOpenGLLate()
{
}
