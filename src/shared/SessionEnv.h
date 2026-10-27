#ifndef SESSIONENV_H
#define SESSIONENV_H

#include <QString>
#include <QStringList>

namespace SessionEnv
{
  bool insideGamescope();

  bool haveX11Display();

  bool haveWaylandDisplay();

  bool insideFlatpak();

  QString drmDriver();

  QStringList v4l2DecodeCodecs();

  // Highest bit depth the stateful decoder can emit on its CAPTURE side, or 0
  // if there is no such decoder. A device that enumerates only 8 bit raw
  // formats will truncate 10 bit content instead of refusing it.
  int v4l2DecodeBitDepth();

  QString v4l2DecodeReport();

  // True when Steam started this process, which is what gives the device its
  // per-title power profile. A launch from the SteamOS Launch Programs menu
  // sets none of these.
  bool launchedFromSteam();

  QString steamGameId();

  QString steamEnvReport();

  void sanitizeGraphicsEnv();

  QString graphicsEnvReport();

  QString description();
}

#endif
