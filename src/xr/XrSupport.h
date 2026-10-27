#ifndef XRSUPPORT_H
#define XRSUPPORT_H

#include <QSize>
#include <QString>
#include <QStringList>

namespace XrSupport
{
  struct Info
  {
    bool compiledIn = false;
    bool runtimeFound = false;
    bool headsetPresent = false;
    bool openglSupported = false;
    QString runtimeName;
    QString runtimeVersion;
    QString systemName;
    QString manifest;
    QString pathRegistry;
    QSize recommendedPerEye;
    int viewCount = 0;
    QStringList extensions;
    QString error;
  };

  bool ensureRuntimeJson();

  bool ensurePathRegistry();

  bool autoProbeAllowed();

  const Info& probe();

  QString report();
}

#endif
