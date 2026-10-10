#include "SessionEnv.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

static QString g_graphicsEnvReport;

bool SessionEnv::insideGamescope()
{
  if (!qgetenv("GAMESCOPE_WAYLAND_DISPLAY").isEmpty())
    return true;

  const QString desktops = QString::fromLocal8Bit(qgetenv("XDG_CURRENT_DESKTOP"));
  for (const QString& name : desktops.split(':', QString::SkipEmptyParts))
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
  }
  return QString();
}

static bool haveVulkanIcd(const QString& nameFilter)
{
  QStringList dirs = {"/usr/share/vulkan/icd.d", "/usr/lib/vulkan/icd.d",
                      "/usr/local/share/vulkan/icd.d"};

  const QString dataDirs = QString::fromLocal8Bit(qgetenv("XDG_DATA_DIRS"));
  for (const QString& dir : dataDirs.split(':', QString::SkipEmptyParts))
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

  if (insideFlatpak() && !qEnvironmentVariableIsSet("PMP_KEEP_MESA_OVERRIDES"))
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

    for (const char* var : {"VK_ICD_FILENAMES", "VK_DRIVER_FILES"})
    {
      const QString value = QString::fromLocal8Bit(qgetenv(var));
      if (!value.isEmpty())
      {
        bool resolves = false;
        for (const QString& path : value.split(':', QString::SkipEmptyParts))
          resolves = resolves || QFile::exists(path);
        if (!resolves)
        {
          report << QString("unset %1 (was %2; no such file in the sandbox)").arg(var, value);
          qunsetenv(var);
        }
      }
    }
  }

  const QString driver = drmDriver();
  if (insideFlatpak() && driver == "msm" && !qEnvironmentVariableIsSet("MESA_LOADER_DRIVER_OVERRIDE"))
  {
    if (haveVulkanIcd("*freedreno*.json"))
    {
      qputenv("MESA_LOADER_DRIVER_OVERRIDE", "zink");
      report << "MESA_LOADER_DRIVER_OVERRIDE=zink (Adreno: matches the vendor's "
                "GL stack; freedreno gallium a7xx is undocumented upstream)";
    }
    else
    {
      report << "Adreno detected but no freedreno Vulkan ICD in the sandbox, "
                "leaving the GL driver to Mesa";
    }
  }

  if (!driver.isEmpty())
    report << QString("drm-driver=%1").arg(driver);

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
