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
    QSize recommendedPerEye;
    int viewCount = 0;
    QStringList extensions;
    QString error;
  };

  // Points the loader at a runtime manifest when the session has none
  // configured. SteamOS installs SteamVR outside the loader's search path and
  // writes its active_runtime.json into the gaming account's home, which a
  // sandboxed app cannot read.
  bool ensureRuntimeJson();

  // True where a runtime is installed at the fixed path SteamOS uses, which is
  // a device whose runtime is already running. Elsewhere creating an instance
  // can start the runtime, so the probe waits until stereo output is asked
  // for.
  bool autoProbeAllowed();

  // Creates an instance and asks for the headset. Cheap enough to run at
  // startup, and the only reliable way to know whether the runtime answers
  // from inside the sandbox.
  const Info& probe();

  QString report();
}

#endif
