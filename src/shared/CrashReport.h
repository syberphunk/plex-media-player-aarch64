#ifndef CRASHREPORT_H
#define CRASHREPORT_H

#include <QString>

namespace CrashReport
{
  void install(const QString& statePath);
  bool captureStandardError(const QString& path);
}

#endif // CRASHREPORT_H
