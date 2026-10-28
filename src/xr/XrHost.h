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

  QProcess* start(const QString& subcommand, QObject* parent, QString* error);

  QJsonObject parseAnswer(const QByteArray& out, const QByteArray& err, int exitCode,
                          QString* error);
}

#endif
