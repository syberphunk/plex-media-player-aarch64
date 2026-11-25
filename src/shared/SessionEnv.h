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

  QString hostOsRelease(const QString& key);

  QString hostOsVersion();

  QString hostOsBuild();

  QString hostOsReport();

  QStringList v4l2DecodeCodecs();

  int v4l2DecodeBitDepth();

  QString v4l2DecodeReport();

  QStringList v4l2DecodeExcludedCodecs();

  bool launchedFromSteam();

  QString steamGameId();

  QString steamEnvReport();

  void sanitizeGraphicsEnv();

  QString graphicsEnvReport();

  QString description();
}

#endif
