#ifndef SESSIONENV_H
#define SESSIONENV_H

#include <QString>

namespace SessionEnv
{
  bool insideGamescope();

  bool haveX11Display();

  bool haveWaylandDisplay();

  bool insideFlatpak();

  QString drmDriver();

  void sanitizeGraphicsEnv();

  QString graphicsEnvReport();

  QString description();
}

#endif
