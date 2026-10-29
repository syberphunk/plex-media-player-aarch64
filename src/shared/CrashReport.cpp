#include "CrashReport.h"

#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace
{
  const size_t kPathMax = 4096;
  const int kFrameMax = 64;

  char g_statePath[kPathMax] = {0};
  const size_t kAltStackSize = 65536;

  char g_altStack[kAltStackSize];
  volatile sig_atomic_t g_reporting = 0;

  void writeAll(int fd, const char* text)
  {
    const size_t length = strlen(text);
    ssize_t written = write(fd, text, length);
    (void)written;
  }

  void writeNumber(int fd, int value)
  {
    char digits[16];
    int at = sizeof(digits);

    if (value == 0)
      digits[--at] = '0';

    while (value > 0 && at > 0)
    {
      digits[--at] = static_cast<char>('0' + (value % 10));
      value /= 10;
    }

    ssize_t written = write(fd, digits + at, sizeof(digits) - at);
    (void)written;
  }

  const char* signalName(int signo)
  {
    switch (signo)
    {
      case SIGSEGV: return "SIGSEGV";
      case SIGABRT: return "SIGABRT";
      case SIGBUS:  return "SIGBUS";
      case SIGILL:  return "SIGILL";
      case SIGFPE:  return "SIGFPE";
      default:      return "signal";
    }
  }

  void recordCrash(int signo)
  {
    if (g_statePath[0] == 0)
      return;

    const int fd = open(g_statePath, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd < 0)
      return;

    writeAll(fd, "crash=");
    writeNumber(fd, signo);
    writeAll(fd, "\n");
    close(fd);
  }

  void handler(int signo, siginfo_t* info, void* context)
  {
    (void)context;

    if (g_reporting)
      _exit(4);
    g_reporting = 1;

    writeAll(STDERR_FILENO, "\nCrash: the process is stopping on ");
    writeAll(STDERR_FILENO, signalName(signo));
    writeAll(STDERR_FILENO, " (");
    writeNumber(STDERR_FILENO, signo);
    writeAll(STDERR_FILENO, ") at address 0x");
    if (info)
    {
      unsigned long address = reinterpret_cast<unsigned long>(info->si_addr);
      char hex[2 * sizeof(address) + 1];
      int at = static_cast<int>(sizeof(hex)) - 1;
      hex[at] = 0;
      if (address == 0)
        hex[--at] = '0';
      while (address != 0 && at > 0)
      {
        const int nibble = static_cast<int>(address & 0xf);
        hex[--at] = static_cast<char>(nibble < 10 ? '0' + nibble : 'a' + (nibble - 10));
        address >>= 4;
      }
      writeAll(STDERR_FILENO, hex + at);
    }
    else
    {
      writeAll(STDERR_FILENO, "?");
    }
    writeAll(STDERR_FILENO, ", backtrace follows\n");

    void* frames[kFrameMax];
    const int count = backtrace(frames, kFrameMax);
    backtrace_symbols_fd(frames, count, STDERR_FILENO);

    recordCrash(signo);

    struct sigaction reset;
    memset(&reset, 0, sizeof(reset));
    reset.sa_handler = SIG_DFL;
    sigemptyset(&reset.sa_mask);
    sigaction(signo, &reset, nullptr);
    raise(signo);
    _exit(4);
  }
}

bool CrashReport::captureStandardError(const QString& path)
{
  const QByteArray target = path.toUtf8();
  if (target.isEmpty())
    return false;

  const int fd = open(target.constData(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    return false;

  const bool ok = dup2(fd, STDERR_FILENO) >= 0;
  close(fd);

  if (ok)
    writeAll(STDERR_FILENO, "Diagnostics: this file holds everything the process wrote to standard error\n");

  return ok;
}

void CrashReport::install(const QString& statePath)
{
  const QByteArray path = statePath.toUtf8();
  if (!path.isEmpty() && static_cast<size_t>(path.size()) < kPathMax)
    memcpy(g_statePath, path.constData(), path.size() + 1);

  stack_t alt;
  memset(&alt, 0, sizeof(alt));
  alt.ss_sp = g_altStack;
  alt.ss_size = sizeof(g_altStack);
  alt.ss_flags = 0;
  sigaltstack(&alt, nullptr);

  void* warm[4];
  backtrace(warm, 4);

  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_sigaction = handler;
  action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
  sigemptyset(&action.sa_mask);

  const int fatalSignals[] = {SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE};
  for (size_t at = 0; at < sizeof(fatalSignals) / sizeof(fatalSignals[0]); at++)
    sigaction(fatalSignals[at], &action, nullptr);
}
