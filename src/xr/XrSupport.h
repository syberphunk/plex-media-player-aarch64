#ifndef XRSUPPORT_H
#define XRSUPPORT_H

#include <QJsonObject>
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
    bool viaHost = false;
    bool sessionReady = false;
    int swapchains = 0;
    QString runtimeName;
    QString runtimeVersion;
    QString systemName;
    QString manifest;
    QString pathRegistry;
    QSize recommendedPerEye;
    int viewCount = 0;
    QStringList extensions;
    QStringList blendModes;
    QString error;
  };

  bool ensureRuntimeJson();

  bool ensurePathRegistry();

  bool autoProbeAllowed();

  const Info& probe();

  bool hostRoute();

  void refreshLocal();

  void applyHostAnswer(const QJsonObject& answer, const QString& error, bool sessionTried);

  QString report();
}

#endif
