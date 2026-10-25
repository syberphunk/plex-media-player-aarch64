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

  QString v4l2DecodeReport();

  void sanitizeGraphicsEnv();

  QString graphicsEnvReport();

  QString description();
}

#endif
