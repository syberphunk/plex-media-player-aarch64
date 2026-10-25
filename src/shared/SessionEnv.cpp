#include "SessionEnv.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStringList>
#include <QSysInfo>

#if defined(Q_OS_LINUX) && __has_include(<linux/videodev2.h>)
#define PMP_HAVE_V4L2 1
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

static QString g_graphicsEnvReport;
static QString g_v4l2DecodeReport;

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

#ifdef PMP_HAVE_V4L2

static const struct { unsigned int fourcc; const char* codec; } k_v4l2CodedFormats[] = {
#ifdef V4L2_PIX_FMT_H264
  {V4L2_PIX_FMT_H264, "h264"},
#endif
#ifdef V4L2_PIX_FMT_HEVC
  {V4L2_PIX_FMT_HEVC, "hevc"},
#endif
#ifdef V4L2_PIX_FMT_VP9
  {V4L2_PIX_FMT_VP9, "vp9"},
#endif
#ifdef V4L2_PIX_FMT_VP8
  {V4L2_PIX_FMT_VP8, "vp8"},
#endif
#ifdef V4L2_PIX_FMT_MPEG2
  {V4L2_PIX_FMT_MPEG2, "mpeg2"},
#endif
#ifdef V4L2_PIX_FMT_MPEG4
  {V4L2_PIX_FMT_MPEG4, "mpeg4"},
#endif
#ifdef V4L2_PIX_FMT_MPEG1
  {V4L2_PIX_FMT_MPEG1, "mpeg1"},
#endif
#ifdef V4L2_PIX_FMT_H263
  {V4L2_PIX_FMT_H263, "h263"},
#endif
#ifdef V4L2_PIX_FMT_VC1_ANNEX_G
  {V4L2_PIX_FMT_VC1_ANNEX_G, "vc1"},
#endif
};

static int v4l2Ioctl(int fd, unsigned long request, void* arg)
{
  int result;
  do
  {
    result = ioctl(fd, request, arg);
  } while (result == -1 && errno == EINTR);
  return result;
}

static void collectV4l2CodedFormats(int fd, unsigned int bufferType, QSet<unsigned int>& formats)
{
  for (unsigned int index = 0;; index++)
  {
    struct v4l2_fmtdesc desc = {};
    desc.index = index;
    desc.type = bufferType;
    if (v4l2Ioctl(fd, VIDIOC_ENUM_FMT, &desc) == -1)
      return;
    formats.insert(desc.pixelformat);
  }
}

static QStringList probeV4l2DecodeCodecs(QStringList& report)
{
  QStringList codecs;

  DIR* dev = opendir("/dev");
  if (!dev)
    return codecs;

  QStringList nodes;
  for (struct dirent* entry = readdir(dev); entry; entry = readdir(dev))
  {
    if (strncmp(entry->d_name, "video", 5) == 0)
      nodes << QString("/dev/") + QString::fromLocal8Bit(entry->d_name);
  }
  closedir(dev);
  nodes.sort();

  for (const QString& path : nodes)
  {
    const int fd = open(QFile::encodeName(path).constData(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
      continue;

    struct v4l2_capability cap = {};
    if (v4l2Ioctl(fd, VIDIOC_QUERYCAP, &cap) == -1)
    {
      close(fd);
      continue;
    }

    const unsigned int caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps
                                                                       : cap.capabilities;
    if (!(caps & (V4L2_CAP_VIDEO_M2M | V4L2_CAP_VIDEO_M2M_MPLANE)))
    {
      close(fd);
      continue;
    }

    QSet<unsigned int> formats;
    collectV4l2CodedFormats(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT, formats);
    collectV4l2CodedFormats(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, formats);
    close(fd);

    QStringList found;
    for (const auto& entry : k_v4l2CodedFormats)
    {
      if (!formats.contains(entry.fourcc))
        continue;
      found << QString::fromLatin1(entry.codec);
      if (!codecs.contains(QString::fromLatin1(entry.codec)))
        codecs << QString::fromLatin1(entry.codec);
    }

    if (found.isEmpty())
      continue;

    report << QString("%1 (%2) %3").arg(path,
                                        QString::fromLatin1((const char*)cap.driver),
                                        found.join(','));
  }

  return codecs;
}

#endif

QStringList SessionEnv::v4l2DecodeCodecs()
{
  static bool probed = false;
  static QStringList codecs;

  if (probed)
    return codecs;
  probed = true;

  if (qEnvironmentVariableIsSet("PMP_NO_V4L2_DECODE"))
  {
    g_v4l2DecodeReport = "disabled by PMP_NO_V4L2_DECODE";
    return codecs;
  }

#ifdef PMP_HAVE_V4L2
  QStringList report;
  codecs = probeV4l2DecodeCodecs(report);
  g_v4l2DecodeReport = report.isEmpty() ? QString("no stateful decoder found")
                                        : report.join("; ");
#else
  g_v4l2DecodeReport = "not built with V4L2 support";
#endif

  return codecs;
}

QString SessionEnv::v4l2DecodeReport()
{
  v4l2DecodeCodecs();
  return g_v4l2DecodeReport;
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
