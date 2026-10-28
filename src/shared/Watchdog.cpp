#include "Watchdog.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace
{
  std::atomic<bool> g_engineLoaded{false};
  std::atomic<bool> g_framePresented{false};
  std::atomic<bool> g_quitting{false};
  std::atomic<bool> g_startupArmed{false};
  std::atomic<bool> g_shutdownArmed{false};

  const int kTickMs = 200;

  void say(const char* text)
  {
    const size_t length = strlen(text);
    ssize_t written = write(STDERR_FILENO, text, length);
    (void)written;
  }

  void record(const std::string& path, const char* line)
  {
    if (path.empty())
      return;

    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd < 0)
      return;

    ssize_t written = write(fd, line, strlen(line));
    (void)written;
    close(fd);
  }

  bool wait(int seconds)
  {
    const int ticks = (seconds * 1000) / kTickMs;
    for (int tick = 0; tick < ticks; tick++)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(kTickMs));
      if (g_framePresented.load() || g_quitting.load())
        return false;
    }
    return true;
  }
}

void Watchdog::armStartup(int seconds, const QString& statePath)
{
  if (g_startupArmed.exchange(true))
    return;

  const std::string path = statePath.toStdString();

  std::thread([seconds, path]() {
    if (!wait(seconds))
      return;

    if (g_engineLoaded.load())
    {
      record(path, "stall=frame\n");
      say("plexmediaplayer: no frame was presented, abandoning this start\n");
    }
    else
    {
      record(path, "stall=load\n");
      say("plexmediaplayer: the interface never loaded, abandoning this start\n");
    }

    _exit(3);
  }).detach();
}

void Watchdog::armShutdown(int seconds)
{
  g_quitting.store(true);

  if (g_shutdownArmed.exchange(true))
    return;

  std::thread([seconds]() {
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    say("plexmediaplayer: shutdown did not finish in time, leaving now\n");
    _exit(0);
  }).detach();
}

void Watchdog::noteEngineLoaded()
{
  g_engineLoaded.store(true);
}

void Watchdog::noteFramePresented()
{
  g_framePresented.store(true);
}

bool Watchdog::framePresented()
{
  return g_framePresented.load();
}
