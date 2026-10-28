#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <QString>

namespace Watchdog
{
  void armStartup(int seconds, const QString& statePath);
  void armShutdown(int seconds);

  void noteEngineLoaded();
  void noteFramePresented();

  bool framePresented();
}

#endif // WATCHDOG_H
