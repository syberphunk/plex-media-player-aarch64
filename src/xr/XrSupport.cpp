#include "XrSupport.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSysInfo>

#include "QsLog.h"

#ifdef HAVE_OPENXR
#include <openxr/openxr.h>
#include <vector>

#define PMP_XR_OPENGL_EXTENSION "XR_KHR_opengl_enable"
#endif

namespace
{
  XrSupport::Info g_info;
  bool g_probed = false;

  QString runtimeManifestCandidate()
  {
    const QString arch = QSysInfo::buildCpuArchitecture();
    QStringList names;
    if (arch == QLatin1String("arm64"))
      names << QStringLiteral("steamxr_linuxarm64.json");
    names << QStringLiteral("steamxr_linux64.json");

    for (const QString& name : names)
    {
      const QString path = QStringLiteral("/opt/steamvr/") + name;
      if (QFileInfo(path).isFile())
        return path;
    }
    return QString();
  }

  bool configuredRuntimeExists()
  {
    if (qEnvironmentVariableIsSet("XR_RUNTIME_JSON"))
      return true;

    QStringList dirs;
    const QString configHome = QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME"));
    dirs << (configHome.isEmpty() ? QDir::homePath() + "/.config" : configHome);
    dirs << QStringLiteral("/etc/xdg") << QStringLiteral("/usr/local/share")
         << QStringLiteral("/usr/share");
    for (const QString& dir : dirs)
    {
      if (QFileInfo(dir + "/openxr/1/active_runtime.json").isFile())
        return true;
    }
    return false;
  }
}

bool XrSupport::autoProbeAllowed()
{
  return !runtimeManifestCandidate().isEmpty();
}

bool XrSupport::ensureRuntimeJson()
{
  if (configuredRuntimeExists())
    return true;

  const QString manifest = runtimeManifestCandidate();
  if (manifest.isEmpty())
    return false;

  qputenv("XR_RUNTIME_JSON", manifest.toLocal8Bit());
  QLOG_INFO() << "OpenXR: no active runtime configured, using" << qPrintable(manifest);
  return true;
}

#ifdef HAVE_OPENXR

namespace
{
  QString resultString(XrInstance instance, XrResult result)
  {
    char buffer[XR_MAX_RESULT_STRING_SIZE] = {0};
    if (instance != XR_NULL_HANDLE && XR_SUCCEEDED(xrResultToString(instance, result, buffer)))
      return QString::fromLatin1(buffer);
    return QString::number(static_cast<int>(result));
  }

  QString versionString(XrVersion version)
  {
    return QString("%1.%2.%3")
             .arg(XR_VERSION_MAJOR(version))
             .arg(XR_VERSION_MINOR(version))
             .arg(XR_VERSION_PATCH(version));
  }
}

const XrSupport::Info& XrSupport::probe()
{
  if (g_probed)
    return g_info;
  g_probed = true;
  g_info.compiledIn = true;

  ensureRuntimeJson();
  g_info.manifest = QString::fromLocal8Bit(qgetenv("XR_RUNTIME_JSON"));

  quint32 extensionCount = 0;
  XrResult result = xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);
  if (XR_FAILED(result))
  {
    g_info.error = QString("the runtime did not answer: %1")
                     .arg(resultString(XR_NULL_HANDLE, result));
    return g_info;
  }

  std::vector<XrExtensionProperties> properties(
    extensionCount, XrExtensionProperties{XR_TYPE_EXTENSION_PROPERTIES, nullptr, {0}, 0});
  if (extensionCount > 0)
  {
    result = xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount,
                                                    properties.data());
    if (XR_FAILED(result))
    {
      g_info.error = QString("the extension list could not be read: %1")
                       .arg(resultString(XR_NULL_HANDLE, result));
      return g_info;
    }
  }

  for (const XrExtensionProperties& property : properties)
    g_info.extensions << QString::fromLatin1(property.extensionName);
  g_info.openglSupported = g_info.extensions.contains(QLatin1String(PMP_XR_OPENGL_EXTENSION));

  const char* const wanted[] = {PMP_XR_OPENGL_EXTENSION};
  XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
  createInfo.applicationInfo.apiVersion = XR_API_VERSION_1_0;
  qstrncpy(createInfo.applicationInfo.applicationName, "Plex Media Player",
           XR_MAX_APPLICATION_NAME_SIZE);
  qstrncpy(createInfo.applicationInfo.engineName, "Qt", XR_MAX_ENGINE_NAME_SIZE);
  createInfo.applicationInfo.applicationVersion = 1;
  createInfo.applicationInfo.engineVersion = 1;
  createInfo.enabledExtensionCount = g_info.openglSupported ? 1 : 0;
  createInfo.enabledExtensionNames = g_info.openglSupported ? wanted : nullptr;

  XrInstance instance = XR_NULL_HANDLE;
  result = xrCreateInstance(&createInfo, &instance);
  if (XR_FAILED(result))
  {
    g_info.error = QString("no instance: %1").arg(resultString(XR_NULL_HANDLE, result));
    return g_info;
  }
  g_info.runtimeFound = true;

  XrInstanceProperties instanceProperties{XR_TYPE_INSTANCE_PROPERTIES};
  if (XR_SUCCEEDED(xrGetInstanceProperties(instance, &instanceProperties)))
  {
    g_info.runtimeName = QString::fromLatin1(instanceProperties.runtimeName);
    g_info.runtimeVersion = versionString(instanceProperties.runtimeVersion);
  }

  XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
  systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
  XrSystemId system = XR_NULL_SYSTEM_ID;
  result = xrGetSystem(instance, &systemInfo, &system);
  if (XR_FAILED(result))
  {
    g_info.error = QString("no headset: %1").arg(resultString(instance, result));
    xrDestroyInstance(instance);
    return g_info;
  }

  XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
  if (XR_SUCCEEDED(xrGetSystemProperties(instance, system, &systemProperties)))
    g_info.systemName = QString::fromLatin1(systemProperties.systemName);

  quint32 viewCount = 0;
  result = xrEnumerateViewConfigurationViews(instance, system,
                                             XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                             0, &viewCount, nullptr);
  if (XR_SUCCEEDED(result) && viewCount > 0)
  {
    std::vector<XrViewConfigurationView> views(
      viewCount, XrViewConfigurationView{XR_TYPE_VIEW_CONFIGURATION_VIEW});
    if (XR_SUCCEEDED(xrEnumerateViewConfigurationViews(
          instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
          viewCount, &viewCount, views.data())))
    {
      g_info.viewCount = static_cast<int>(viewCount);
      g_info.recommendedPerEye = QSize(views[0].recommendedImageRectWidth,
                                       views[0].recommendedImageRectHeight);
    }
  }
  else if (XR_FAILED(result))
  {
    g_info.error = QString("no stereo view configuration: %1")
                     .arg(resultString(instance, result));
  }

  g_info.headsetPresent = g_info.viewCount >= 2;
  xrDestroyInstance(instance);
  return g_info;
}

#else

const XrSupport::Info& XrSupport::probe()
{
  if (!g_probed)
  {
    g_probed = true;
    g_info.error = QStringLiteral("built without OpenXR");
  }
  return g_info;
}

#endif

QString XrSupport::report()
{
  const Info& info = probe();
  QStringList parts;
  if (!info.compiledIn)
    return QStringLiteral("not built in");

  parts << QString("manifest=%1").arg(info.manifest.isEmpty() ? QString("(discovered)")
                                                              : info.manifest);
  parts << QString("runtime=%1").arg(info.runtimeFound
                                       ? QString("%1 %2").arg(info.runtimeName, info.runtimeVersion)
                                       : QString("none"));
  if (info.runtimeFound)
  {
    parts << QString("headset=%1").arg(info.headsetPresent ? info.systemName : QString("none"));
    if (info.headsetPresent)
    {
      parts << QString("views=%1").arg(info.viewCount);
      parts << QString("per-eye=%1x%2").arg(info.recommendedPerEye.width())
                                       .arg(info.recommendedPerEye.height());
    }
    parts << QString("opengl=%1").arg(info.openglSupported ? "yes" : "no");
    parts << QString("extensions=%1").arg(info.extensions.size());
  }
  if (!info.error.isEmpty())
    parts << QString("error=\"%1\"").arg(info.error);
  return parts.join(" ");
}
