#include "XrHost.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>

#include <fcntl.h>
#include <unistd.h>

#include "QsLog.h"
#include "XrProtocol.h"

namespace
{
  const char* kFlatpakInfo = "/.flatpak-info";
  const char* kPortalName = "org.freedesktop.Flatpak";
  const char* kHelperName = "pmp-xr-host";
  const int kForwardFd = XrProtocol::kSocketFd;

  bool shellSafe(const QString& path)
  {
    for (const QChar c : path)
    {
      if (c == QLatin1Char('"') || c == QLatin1Char('\'') || c == QLatin1Char('$') ||
          c == QLatin1Char('`') || c == QLatin1Char('\\') || c.unicode() < 0x20)
        return false;
    }
    return !path.isEmpty();
  }

  QString instanceValue(const QString& key)
  {
    static QSettings* info = nullptr;
    if (!info)
    {
      if (!QFileInfo(QString::fromLatin1(kFlatpakInfo)).isReadable())
        return QString();
      info = new QSettings(QString::fromLatin1(kFlatpakInfo), QSettings::IniFormat);
    }
    return info->value(QStringLiteral("Instance/") + key).toString();
  }

  QString multiarchTriplet()
  {
    const QString arch = QSysInfo::buildCpuArchitecture();
    if (arch == QLatin1String("arm64"))
      return QStringLiteral("aarch64-linux-gnu");
    if (arch == QLatin1String("x86_64"))
      return QStringLiteral("x86_64-linux-gnu");
    return QString();
  }

  QString runtimeManifest()
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
}

bool XrHost::inSandbox()
{
  return QFileInfo(QString::fromLatin1(kFlatpakInfo)).isReadable();
}

QString XrHost::appPath()
{
  return instanceValue(QStringLiteral("app-path"));
}

QString XrHost::runtimePath()
{
  return instanceValue(QStringLiteral("runtime-path"));
}

QStringList XrHost::helperCandidates()
{
  const QString app = appPath();
  if (app.isEmpty())
    return QStringList();

  return QStringList{app + QStringLiteral("/bin/") + QLatin1String(kHelperName),
                     app + QStringLiteral("/files/bin/") + QLatin1String(kHelperName)};
}

QString XrHost::helperPath()
{
  const QStringList candidates = helperCandidates();
  return candidates.isEmpty() ? QString() : candidates.first();
}

QStringList XrHost::libraryPath()
{
  QStringList dirs;
  const QString triplet = multiarchTriplet();
  const QString app = appPath();
  if (app.isEmpty())
    return dirs;

  for (const QString& prefix : {QStringLiteral("/lib"), QStringLiteral("/files/lib")})
  {
    dirs << app + prefix;
    if (!triplet.isEmpty())
      dirs << app + prefix + QLatin1Char('/') + triplet;
  }
  return dirs;
}

QStringList XrHost::spawnCommand(const QString& subcommand, int forwardFd)
{
  QStringList candidates;
  for (const QString& path : helperCandidates())
  {
    if (shellSafe(path))
      candidates << path;
  }
  if (candidates.isEmpty())
    return QStringList();

  QStringList args;
  args << QStringLiteral("--host");
  if (forwardFd >= 0)
    args << QStringLiteral("--forward-fd=%1").arg(forwardFd);
  args << QStringLiteral("--env=LD_LIBRARY_PATH=") + libraryPath().join(QLatin1Char(':'));

  const QString manifest = runtimeManifest();
  if (!manifest.isEmpty())
    args << QStringLiteral("--env=XR_RUNTIME_JSON=") + manifest;

  QStringList quoted;
  for (const QString& path : candidates)
    quoted << QLatin1Char('"') + path + QLatin1Char('"');

  const QString script = QStringLiteral("for p in %1; do [ -x \"$p\" ] && exec \"$p\" \"$1\"; "
                                        "done; echo \"%2 is not on the host\" >&2; exit 3")
                           .arg(quoted.join(QLatin1Char(' ')), QLatin1String(kHelperName));

  args << QStringLiteral("/bin/sh") << QStringLiteral("-c") << script
       << QLatin1String(kHelperName) << subcommand;
  return args;
}

bool XrHost::available()
{
  if (!inSandbox())
    return false;

  if (helperPath().isEmpty())
    return false;
  return !QStandardPaths::findExecutable(QStringLiteral("flatpak-spawn")).isEmpty();
}

QProcess* XrHost::start(const QString& subcommand, QObject* parent, QString* error)
{
  const auto reportError = [error](const QString& text) -> QProcess* {
    if (error)
      *error = text;
    return nullptr;
  };

  if (!inSandbox())
    return reportError(QStringLiteral("not sandboxed"));

  const QString spawn = QStandardPaths::findExecutable(QStringLiteral("flatpak-spawn"));
  if (spawn.isEmpty())
    return reportError(QStringLiteral("flatpak-spawn is missing from the runtime"));

  const QStringList args = spawnCommand(subcommand);
  if (args.isEmpty())
    return reportError(QStringLiteral("the app deploy path is unknown"));

  QProcess* process = new QProcess(parent);
  process->setProgram(spawn);
  process->setArguments(args);
  process->start(QIODevice::ReadOnly);

  if (error)
    error->clear();
  return process;
}

QProcess* XrHost::startWithFd(const QString& subcommand, int fd, QObject* parent, QString* error)
{
  const auto reportError = [error](const QString& text) -> QProcess* {
    if (error)
      *error = text;
    return nullptr;
  };

  if (!inSandbox())
    return reportError(QStringLiteral("not sandboxed"));

  if (fd < 0)
    return reportError(QStringLiteral("there is no channel to hand over"));

  const QString spawn = QStandardPaths::findExecutable(QStringLiteral("flatpak-spawn"));
  if (spawn.isEmpty())
    return reportError(QStringLiteral("flatpak-spawn is missing from the runtime"));

  const QStringList args = spawnCommand(subcommand, kForwardFd);
  if (args.isEmpty())
    return reportError(QStringLiteral("the app deploy path is unknown"));

  QProcess* process = new QProcess(parent);
  process->setProgram(spawn);
  process->setArguments(args);
  process->setChildProcessModifier([fd]() {
    if (fd != kForwardFd && dup2(fd, kForwardFd) < 0)
      _exit(127);
    const int flags = fcntl(kForwardFd, F_GETFD);
    if (flags >= 0)
      fcntl(kForwardFd, F_SETFD, flags & ~FD_CLOEXEC);
  });
  process->start(QIODevice::ReadOnly);

  if (error)
    error->clear();
  return process;
}

QJsonObject XrHost::parseAnswer(const QByteArray& out, const QByteArray& err, int exitCode,
                                QString* error)
{
  const auto reportError = [error](const QString& text) {
    if (error)
      *error = text;
    return QJsonObject();
  };

  if (out.isEmpty())
  {
    QString text = QString::fromUtf8(err).trimmed();
    if (text.contains(QLatin1String("org.freedesktop.DBus.Error.ServiceUnknown")) ||
        text.contains(QLatin1String("--host only works")))
    {
      text = QStringLiteral("the host portal is not permitted (needs --talk-name=%1)")
               .arg(QString::fromLatin1(kPortalName));
    }
    if (text.isEmpty())
      text = QStringLiteral("the host helper produced no output (exit %1)").arg(exitCode);
    return reportError(text);
  }

  QJsonParseError parseError{};
  const QJsonDocument document = QJsonDocument::fromJson(out, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject())
    return reportError(QStringLiteral("the host helper answer could not be read: %1")
                         .arg(parseError.errorString()));

  if (!err.isEmpty())
    QLOG_DEBUG() << "OpenXR host helper stderr:" << QString::fromUtf8(err).trimmed();

  if (error)
    error->clear();
  return document.object();
}
