#include "SessionEnv.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QSysInfo>

static QString g_graphicsEnvReport;

bool SessionEnv::insideGamescope()
{
  if (!qgetenv("GAMESCOPE_WAYLAND_DISPLAY").isEmpty())
    return true;

  const QString desktops = QString::fromLocal8Bit(qgetenv("XDG_CURRENT_DESKTOP"));
  for (const QString& name : desktops.split(':', Qt::SkipEmptyParts))
  {
    if (name.compare("gamescope", Qt::CaseInsensitive) == 0)
      return true;
  }

  return false;
}

bool SessionEnv::haveX11Display()
{
  return !qgetenv("DISPLAY").isEmpty();
}

bool SessionEnv::haveWaylandDisplay()
{
  return !qgetenv("WAYLAND_DISPLAY").isEmpty();
}

bool SessionEnv::insideFlatpak()
{
  return qEnvironmentVariableIsSet("FLATPAK_ID") || QFile::exists("/.flatpak-info");
}

QString SessionEnv::drmDriver()
{
  const QDir drm("/sys/class/drm");
  QStringList cards = drm.entryList({"card[0-9]*"}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
  for (const QString& card : cards)
  {
    const QString device = drm.absoluteFilePath(card) + "/device";
    const QString driver = QFileInfo(device + "/driver").symLinkTarget();
    if (!driver.isEmpty())
      return QFileInfo(driver).fileName();

    QFile uevent(device + "/uevent");
    if (uevent.open(QIODevice::ReadOnly | QIODevice::Text))
    {
      for (const QByteArray& line : uevent.readAll().split('\n'))
      {
        if (line.startsWith("DRIVER="))
          return QString::fromLatin1(line.mid(7)).trimmed();
      }
    }
  }
  return QString();
}

static bool haveVulkanIcd(const QString& nameFilter)
{
  QStringList dirs = {"/usr/share/vulkan/icd.d", "/usr/lib/vulkan/icd.d",
                      "/usr/local/share/vulkan/icd.d"};

  const QString dataDirs = QString::fromLocal8Bit(qgetenv("XDG_DATA_DIRS"));
  for (const QString& dir : dataDirs.split(':', Qt::SkipEmptyParts))
    dirs << dir + "/vulkan/icd.d";

  const QDir libDir("/usr/lib");
  for (const QString& triplet : libDir.entryList({"*-linux-gnu*"}, QDir::Dirs | QDir::NoDotAndDotDot))
  {
    const QDir glDir("/usr/lib/" + triplet + "/GL");
    for (const QString& variant : glDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
    {
      const QString base = glDir.absoluteFilePath(variant);
      dirs << base + "/vulkan/icd.d" << base + "/lib/vulkan/icd.d"
           << base + "/share/vulkan/icd.d";
    }
  }

  for (const QString& dir : dirs)
  {
    if (!QDir(dir).entryList({nameFilter}, QDir::Files).isEmpty())
      return true;
  }
  return false;
}

void SessionEnv::sanitizeGraphicsEnv()
{
  QStringList report;

  const QString driver = drmDriver();
  const bool adreno = driver == "msm";
  const bool armFlatpak = insideFlatpak() && QSysInfo::buildCpuArchitecture() == "arm64";
  const bool clampTarget = adreno || armFlatpak;
  const bool driverFromEnv = qEnvironmentVariableIsSet("MESA_LOADER_DRIVER_OVERRIDE");

  if (insideFlatpak() && !qEnvironmentVariableIsSet("PMP_KEEP_MESA_OVERRIDES"))
  {
    if (clampTarget)
    {
      const struct { const char* name; const char* value; } clamps[] = {
        {"MESA_GL_VERSION_OVERRIDE", "4.3"},
        {"MESA_GLSL_VERSION_OVERRIDE", "430"},
      };

      for (const auto& clamp : clamps)
      {
        if (qEnvironmentVariableIsSet(clamp.name))
          continue;
        qputenv(clamp.name, clamp.value);
        report << QString("%1=%2 (the GL version the platform exposes to every "
                          "other client)").arg(clamp.name, clamp.value);
      }
    }
    else
    {
      for (const char* var : {"MESA_GL_VERSION_OVERRIDE", "MESA_GLSL_VERSION_OVERRIDE"})
      {
        if (qEnvironmentVariableIsSet(var))
        {
          report << QString("unset %1 (was %2; host value, foreign driver)")
                      .arg(var, QString::fromLocal8Bit(qgetenv(var)));
          qunsetenv(var);
        }
      }
    }

    for (const char* var : {"VK_ICD_FILENAMES", "VK_DRIVER_FILES"})
    {
      const QString value = QString::fromLocal8Bit(qgetenv(var));
      if (!value.isEmpty())
      {
        bool resolves = false;
        for (const QString& path : value.split(':', Qt::SkipEmptyParts))
          resolves = resolves || QFile::exists(path);
        if (!resolves)
        {
          report << QString("unset %1 (was %2; no such file in the sandbox)").arg(var, value);
          qunsetenv(var);
        }
      }
    }
  }

  if (clampTarget && !driverFromEnv && !qEnvironmentVariableIsSet("PMP_KEEP_MESA_OVERRIDES"))
  {
    const QString icd = haveVulkanIcd("*freedreno*.json") ? QString("freedreno")
                      : haveVulkanIcd("*_icd*.json") ? QString("generic")
                      : QString();
    if (!icd.isEmpty())
    {
      qputenv("MESA_LOADER_DRIVER_OVERRIDE", "zink");
      report << QString("MESA_LOADER_DRIVER_OVERRIDE=zink (%1 Vulkan ICD; the "
                        "gallium driver mis-renders the compositor)").arg(icd);
    }
    else
    {
      report << "no Vulkan ICD in the sandbox, leaving the GL driver to Mesa";
    }
  }

  report << QString("drm-driver=%1").arg(driver.isEmpty() ? QString("(none)") : driver);
  report << QString("arch=%1").arg(QSysInfo::buildCpuArchitecture());

  if (driverFromEnv)
  {
    report << QString("MESA_LOADER_DRIVER_OVERRIDE=%1 (from the environment)")
                .arg(QString::fromLocal8Bit(qgetenv("MESA_LOADER_DRIVER_OVERRIDE")));
  }

  g_graphicsEnvReport = report.join("; ");
}

QString SessionEnv::graphicsEnvReport()
{
  return g_graphicsEnvReport;
}

QString SessionEnv::description()
{
  QStringList parts;
  parts << QString("session-type=%1").arg(QString::fromLocal8Bit(qgetenv("XDG_SESSION_TYPE")));
  parts << QString("desktop=%1").arg(QString::fromLocal8Bit(qgetenv("XDG_CURRENT_DESKTOP")));
  parts << QString("DISPLAY=%1").arg(QString::fromLocal8Bit(qgetenv("DISPLAY")));
  parts << QString("WAYLAND_DISPLAY=%1").arg(QString::fromLocal8Bit(qgetenv("WAYLAND_DISPLAY")));
  parts << QString("gamescope=%1").arg(insideGamescope() ? "yes" : "no");
  return parts.join(" ");
}
