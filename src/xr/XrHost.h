#ifndef XRHOST_H
#define XRHOST_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

class QObject;
class QProcess;

namespace XrHost
{
  bool inSandbox();

  QString appPath();

  QString runtimePath();

  QStringList helperCandidates();

  QString helperPath();

  QStringList libraryPath();

  QStringList spawnCommand(const QString& subcommand);

  bool available();

  // Starts the helper on the host. The caller owns the process and reads it
  // when it finishes. Returns nullptr with error set when it cannot be run.
  QProcess* start(const QString& subcommand, QObject* parent, QString* error);

  // Turns what the helper wrote into its answer, or leaves error set.
  QJsonObject parseAnswer(const QByteArray& out, const QByteArray& err, int exitCode,
                          QString* error);
}

#endif
