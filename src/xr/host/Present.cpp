#include "Present.h"

#include "OpenVr.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../XrProtocol.h"

extern "C" char** environ;

namespace
{
  const char kPanelPrefix[] = "valve.steam.desktopgame.";
  const uint32_t kStereoFlag = VROverlayFlags_SideBySide_Parallel;

  const int kScanIndexes = 128;
  const int kScanAhead = 8;
  const int kScanSlice = 512;
  const int kLoopGapMs = 50;
  const int kCheckGapMs = 1000;
  const int kLookGapMs = 1000;
  const int kLookLimit = 30;

  char kSeedKey[] = "io.github.syberphunk.PlexMediaPlayer.seed";
  char kSeedName[] = "Plex Media Player seed";

  volatile sig_atomic_t g_stop = 0;

  void onSignal(int)
  {
    g_stop = 1;
  }

  void note(const char* format, ...) __attribute__((format(printf, 1, 2)));

  void note(const char* format, ...)
  {
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fputc('\n', stderr);
    std::fflush(stderr);
  }

  int64_t nowMs()
  {
    struct timespec now = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<int64_t>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
  }

  uint32_t appidFromEnvironment()
  {
    static const char* const names[] = {"SteamGameId", "SteamAppId", "SteamOverlayGameId"};

    for (const char* name : names)
    {
      const char* text = getenv(name);
      if (!text || !*text)
        continue;

      const unsigned long long value = strtoull(text, nullptr, 10);
      if (value > 0xffffffffull)
        return static_cast<uint32_t>(value >> 32);
      if (value != 0)
        return static_cast<uint32_t>(value);
    }

    return 0;
  }

  struct Neighbour
  {
    VROverlayHandle_t handle = k_ulOverlayHandleInvalid;
    char key[128] = {0};
    char name[128] = {0};
    float width = 0.0f;
    float curvature = 0.0f;
    uint32_t flags = 0;
    uint32_t sortOrder = 0;
    uint32_t pid = 0;
    uint32_t textureWidth = 0;
    uint32_t textureHeight = 0;
    int transformType = -1;
    bool visible = false;
  };

  class Presenter
  {
  public:
    explicit Presenter(int socketFd) : m_socket(socketFd) {}
    ~Presenter() { teardown(); }

    bool setup();
    int run();

  private:
    enum Stage
    {
      Looking = 0,
      Holding = 1,
      Giving = 2
    };

    bool startRuntime();
    void teardown();

    bool readSocket();
    void send(XrProtocol::Type type, const char* reason);

    void pollEvents(bool& exit);
    void describeEnvironment();
    const char* errorName(EVROverlayError error) const;

    bool readNeighbour(VROverlayHandle_t handle, Neighbour& out) const;
    bool findByKey();
    void adopt(const Neighbour& found);
    void look();
    bool seedScan();
    void surveySlice();

    bool assertStereo(std::string& why);
    void dropStereo();
    void hold();

    int m_socket = -1;
    OpenVr::Api m_ovr;

    uint32_t m_appid = 0;
    VROverlayHandle_t m_panel = k_ulOverlayHandleInvalid;
    uint32_t m_panelFlags = 0;
    bool m_panelWasStereo = false;
    bool m_stereo = false;
    bool m_told = false;

    Stage m_stage = Looking;
    int m_lookTries = 0;
    int64_t m_lookAt = 0;
    int64_t m_checkAt = 0;

    bool m_environmentDescribed = false;
    bool m_surveyDone = false;
    bool m_surveyRunning = false;
    uint32_t m_scanGeneration = 0;
    uint32_t m_scanIndex = 0;
    int m_scanSeen = 0;
    int m_scanProbes = 0;
  };

  const char* Presenter::errorName(EVROverlayError error) const
  {
    struct VR_IVROverlay_FnTable* ovl = m_ovr.overlay;
    if (ovl && ovl->GetOverlayErrorNameFromEnum)
    {
      const char* text = ovl->GetOverlayErrorNameFromEnum(error);
      if (text && *text)
        return text;
    }
    return "an unnamed overlay error";
  }

  bool Presenter::startRuntime()
  {
    if (!m_ovr.start(EVRApplicationType_VRApplication_Overlay))
    {
      note("pmp-xr-host: %s", m_ovr.error().c_str());
      return false;
    }

    if (!m_ovr.overlay)
    {
      note("pmp-xr-host: the runtime does not serve %s", IVROverlay_Version);
      return false;
    }

    note("pmp-xr-host: runtime %s, overlay application", m_ovr.libraryPath().c_str());
    return true;
  }

  void Presenter::describeEnvironment()
  {
    m_environmentDescribed = true;

    for (char** entry = environ; entry && *entry; ++entry)
    {
      if (std::strncmp(*entry, "GAMESCOPE_", 10) != 0 &&
          std::strncmp(*entry, "STEAM_GAMESCOPE_", 16) != 0)
        continue;
      note("pmp-xr-host: session %s", *entry);
    }
  }

  bool Presenter::readNeighbour(VROverlayHandle_t handle, Neighbour& out) const
  {
    struct VR_IVROverlay_FnTable* ovl = m_ovr.overlay;

    EVROverlayError error = EVROverlayError_VROverlayError_None;
    const uint32_t length = ovl->GetOverlayKey(handle, out.key, sizeof(out.key), &error);
    if (error != EVROverlayError_VROverlayError_None || length == 0 || out.key[0] == '\0')
      return false;

    out.handle = handle;

    EVROverlayError nameError = EVROverlayError_VROverlayError_None;
    if (ovl->GetOverlayName)
      ovl->GetOverlayName(handle, out.name, sizeof(out.name), &nameError);
    if (nameError != EVROverlayError_VROverlayError_None)
      out.name[0] = '\0';

    if (ovl->GetOverlayWidthInMeters)
      ovl->GetOverlayWidthInMeters(handle, &out.width);
    if (ovl->GetOverlayCurvature)
      ovl->GetOverlayCurvature(handle, &out.curvature);
    if (ovl->GetOverlayFlags)
      ovl->GetOverlayFlags(handle, &out.flags);
    if (ovl->GetOverlaySortOrder)
      ovl->GetOverlaySortOrder(handle, &out.sortOrder);
    if (ovl->GetOverlayRenderingPid)
      out.pid = ovl->GetOverlayRenderingPid(handle);
    if (ovl->IsOverlayVisible)
      out.visible = ovl->IsOverlayVisible(handle);
    if (ovl->GetOverlayTextureSize)
      ovl->GetOverlayTextureSize(handle, &out.textureWidth, &out.textureHeight);

    VROverlayTransformType type = VROverlayTransformType_VROverlayTransform_Invalid;
    if (ovl->GetOverlayTransformType &&
        ovl->GetOverlayTransformType(handle, &type) == EVROverlayError_VROverlayError_None)
      out.transformType = static_cast<int>(type);

    return true;
  }

  bool Presenter::findByKey()
  {
    struct VR_IVROverlay_FnTable* ovl = m_ovr.overlay;
    if (!ovl->FindOverlay || m_appid == 0)
      return false;

    char key[128] = {0};
    std::snprintf(key, sizeof(key), "%s%u", kPanelPrefix, m_appid);

    VROverlayHandle_t handle = k_ulOverlayHandleInvalid;
    const EVROverlayError error = ovl->FindOverlay(key, &handle);
    if (error != EVROverlayError_VROverlayError_None || handle == k_ulOverlayHandleInvalid)
      return false;

    Neighbour found;
    if (!readNeighbour(handle, found) || std::strcmp(found.key, key) != 0)
      return false;

    adopt(found);
    return true;
  }

  void Presenter::adopt(const Neighbour& found)
  {
    m_panel = found.handle;
    m_panelFlags = found.flags;
    m_panelWasStereo = (found.flags & kStereoFlag) != 0;

    note("pmp-xr-host: the Plex panel is overlay %llu \"%s\" named \"%s\", %.2f m wide, "
         "texture %ux%u, %s, flags 0x%x, transform %d, pid %u",
         static_cast<unsigned long long>(found.handle), found.key, found.name, found.width,
         found.textureWidth, found.textureHeight, found.visible ? "showing" : "hidden",
         found.flags, found.transformType, found.pid);
  }

  bool Presenter::seedScan()
  {
    struct VR_IVROverlay_FnTable* ovl = m_ovr.overlay;

    VROverlayHandle_t seed = k_ulOverlayHandleInvalid;
    EVROverlayError error = ovl->CreateOverlay(kSeedKey, kSeedName, &seed);
    if (error == EVROverlayError_VROverlayError_KeyInUse && ovl->FindOverlay)
      error = ovl->FindOverlay(kSeedKey, &seed);

    if (seed == k_ulOverlayHandleInvalid)
    {
      note("pmp-xr-host: no handle to count from (%s)", errorName(error));
      return false;
    }

    m_scanGeneration = static_cast<uint32_t>(seed >> 32) + kScanAhead;
    m_scanIndex = 0;
    m_scanSeen = 0;
    m_scanProbes = 0;

    if (ovl->DestroyOverlay)
      ovl->DestroyOverlay(seed);

    return true;
  }

  void Presenter::surveySlice()
  {
    struct VR_IVROverlay_FnTable* ovl = m_ovr.overlay;
    if (m_surveyDone || !ovl || !ovl->GetOverlayKey)
      return;

    if (!m_surveyRunning)
    {
      if (!m_environmentDescribed)
        describeEnvironment();

      if (!seedScan())
      {
        m_surveyDone = true;
        return;
      }

      m_surveyRunning = true;
      note("pmp-xr-host: listing the overlays in the session, generation %u down to 0",
           m_scanGeneration);
    }

    for (int budget = 0; budget < kScanSlice; ++budget)
    {
      const VROverlayHandle_t handle =
        (static_cast<VROverlayHandle_t>(m_scanGeneration) << 32) | m_scanIndex;

      ++m_scanProbes;

      bool last = false;
      if (++m_scanIndex >= static_cast<uint32_t>(kScanIndexes))
      {
        m_scanIndex = 0;
        if (m_scanGeneration == 0)
          last = true;
        else
          --m_scanGeneration;
      }

      Neighbour found;
      if (readNeighbour(handle, found))
      {
        ++m_scanSeen;

        if (m_panel == k_ulOverlayHandleInvalid && found.visible &&
            std::strncmp(found.key, kPanelPrefix, sizeof(kPanelPrefix) - 1) == 0 &&
            std::strcmp(found.key + sizeof(kPanelPrefix) - 1, "0") != 0)
        {
          note("pmp-xr-host: no key lookup worked, but this overlay looks like the panel");
          adopt(found);
          m_appid = static_cast<uint32_t>(
            strtoul(found.key + sizeof(kPanelPrefix) - 1, nullptr, 10));
          m_lookTries = 0;
          m_lookAt = 0;
        }

        note("pmp-xr-host: overlay %llu \"%s\" named \"%s\", %.2f m wide, texture %ux%u, %s, "
             "flags 0x%x, sort %u, transform %d, curve %.3f, pid %u",
             static_cast<unsigned long long>(handle), found.key, found.name, found.width,
             found.textureWidth, found.textureHeight, found.visible ? "showing" : "hidden",
             found.flags, found.sortOrder, found.transformType, found.curvature, found.pid);
      }

      if (last)
      {
        m_surveyRunning = false;
        m_surveyDone = true;
        note("pmp-xr-host: %d of %d handles are live overlays", m_scanSeen, m_scanProbes);
        return;
      }
    }
  }

  void Presenter::look()
  {
    const int64_t now = nowMs();
    if (m_lookTries > 0 && now - m_lookAt < kLookGapMs)
      return;

    m_lookAt = now;
    ++m_lookTries;

    if (m_panel != k_ulOverlayHandleInvalid || findByKey())
    {
      std::string why;
      if (assertStereo(why))
      {
        m_stage = Holding;
        m_checkAt = nowMs();
        send(XrProtocol::Stereo, nullptr);
        m_told = true;
        note("pmp-xr-host: the panel is side by side, flags 0x%x", m_panelFlags);
        return;
      }

      note("pmp-xr-host: the panel would not go side by side: %s", why.c_str());
      m_panel = k_ulOverlayHandleInvalid;
      m_stage = Giving;
      send(XrProtocol::Failed, why.c_str());
      m_told = true;
      return;
    }

    if (m_lookTries == 1)
      note("pmp-xr-host: looking for overlay %s%u", kPanelPrefix, m_appid);

    if (m_lookTries >= kLookLimit)
    {
      char why[XrProtocol::kReasonLength] = {0};
      std::snprintf(why, sizeof(why), "the Plex panel %s%u is not an overlay in this session",
                    kPanelPrefix, m_appid);
      note("pmp-xr-host: %s", why);
      m_stage = Giving;
      send(XrProtocol::Flat, why);
      m_told = true;
    }
  }

  bool Presenter::assertStereo(std::string& why)
  {
    struct VR_IVROverlay_FnTable* ovl = m_ovr.overlay;
    if (!ovl->SetOverlayFlag || !ovl->GetOverlayFlag)
    {
      why = "the runtime cannot be asked about overlay flags";
      return false;
    }

    const EVROverlayError set =
      ovl->SetOverlayFlag(m_panel, VROverlayFlags_SideBySide_Parallel, true);

    bool enabled = false;
    const EVROverlayError read =
      ovl->GetOverlayFlag(m_panel, VROverlayFlags_SideBySide_Parallel, &enabled);

    if (ovl->GetOverlayFlags)
      ovl->GetOverlayFlags(m_panel, &m_panelFlags);

    if (set != EVROverlayError_VROverlayError_None)
    {
      why = std::string("the headset refused the side by side flag: ") + errorName(set);
      return false;
    }

    if (read != EVROverlayError_VROverlayError_None)
    {
      why = std::string("the side by side flag could not be read back: ") + errorName(read);
      return false;
    }

    if (!enabled)
    {
      why = "the side by side flag did not stick on the Plex panel";
      return false;
    }

    m_stereo = true;
    return true;
  }

  void Presenter::dropStereo()
  {
    struct VR_IVROverlay_FnTable* ovl = m_ovr.overlay;
    if (!m_stereo || !ovl || !ovl->SetOverlayFlag || m_panel == k_ulOverlayHandleInvalid)
    {
      m_stereo = false;
      return;
    }

    m_stereo = false;
    if (m_panelWasStereo)
      return;

    const EVROverlayError error =
      ovl->SetOverlayFlag(m_panel, VROverlayFlags_SideBySide_Parallel, false);
    note("pmp-xr-host: the panel is flat again%s",
         error == EVROverlayError_VROverlayError_None ? "" : " (the runtime complained)");
  }

  void Presenter::hold()
  {
    const int64_t now = nowMs();
    if (now - m_checkAt < kCheckGapMs)
      return;
    m_checkAt = now;

    struct VR_IVROverlay_FnTable* ovl = m_ovr.overlay;

    bool enabled = false;
    const EVROverlayError read =
      ovl->GetOverlayFlag(m_panel, VROverlayFlags_SideBySide_Parallel, &enabled);

    if (read != EVROverlayError_VROverlayError_None)
    {
      note("pmp-xr-host: the Plex panel went away (%s), looking for it again", errorName(read));
      m_stereo = false;
      m_panel = k_ulOverlayHandleInvalid;
      m_stage = Looking;
      m_lookTries = 0;
      send(XrProtocol::Flat, "the Plex panel was replaced, waiting for the new one");
      return;
    }

    if (enabled)
      return;

    note("pmp-xr-host: something cleared the side by side flag, setting it again");

    std::string why;
    if (assertStereo(why))
      return;

    note("pmp-xr-host: %s", why.c_str());
    m_stage = Giving;
    send(XrProtocol::Flat, why.c_str());
  }

  void Presenter::send(XrProtocol::Type type, const char* reason)
  {
    if (m_socket < 0)
      return;

    XrProtocol::Message message = XrProtocol::make(type);
    message.flags = m_panelFlags;
    message.appid = m_appid;
    if (reason && *reason)
      std::snprintf(message.reason, sizeof(message.reason), "%s", reason);

    if (::send(m_socket, &message, sizeof(message), MSG_NOSIGNAL | MSG_DONTWAIT) < 0 &&
        errno != EAGAIN && errno != EWOULDBLOCK)
      note("pmp-xr-host: the app connection broke while answering");
  }

  bool Presenter::readSocket()
  {
    while (true)
    {
      XrProtocol::Message message{};

      const ssize_t got = recv(m_socket, &message, sizeof(message), MSG_DONTWAIT);
      if (got == 0)
      {
        note("pmp-xr-host: the app closed the connection");
        return false;
      }
      if (got < 0)
      {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
          return true;
        if (errno == EINTR)
          continue;
        note("pmp-xr-host: the app connection failed (%s)", std::strerror(errno));
        return false;
      }

      if (got != static_cast<ssize_t>(sizeof(message)) || !XrProtocol::valid(message))
      {
        note("pmp-xr-host: a message of %zd bytes was not understood", got);
        continue;
      }

      switch (message.type)
      {
        case XrProtocol::Panel:
          if (message.appid != 0 && message.appid != m_appid)
          {
            m_appid = message.appid;
            note("pmp-xr-host: the app says its Steam id is %u", m_appid);
            if (m_stage != Holding)
            {
              m_stage = Looking;
              m_lookTries = 0;
            }
          }
          if (m_stage == Holding && m_told)
            send(XrProtocol::Stereo, nullptr);
          break;

        case XrProtocol::Stop:
          note("pmp-xr-host: the app asked to stop");
          return false;

        default:
          break;
      }
    }
  }

  void Presenter::pollEvents(bool& exit)
  {
    if (!m_ovr.system || !m_ovr.system->PollNextEvent)
      return;

    struct VREvent_t event = {};
    while (m_ovr.system->PollNextEvent(&event, sizeof(event)))
    {
      if (event.eventType == EVREventType_VREvent_Quit)
      {
        note("pmp-xr-host: the runtime asked us to quit");
        if (m_ovr.system->AcknowledgeQuit_Exiting)
          m_ovr.system->AcknowledgeQuit_Exiting();
        exit = true;
        return;
      }
    }
  }

  bool Presenter::setup()
  {
    m_appid = appidFromEnvironment();
    if (m_appid != 0)
      note("pmp-xr-host: this session is Steam app %u", m_appid);

    return startRuntime();
  }

  int Presenter::run()
  {
    bool exit = false;

    while (!exit && !g_stop)
    {
      if (!readSocket())
        break;

      pollEvents(exit);
      if (exit)
        break;

      if (m_stage == Looking)
        look();
      else if (m_stage == Holding)
        hold();

      if (m_stage == Looking && m_lookTries >= 1)
        surveySlice();

      if (m_stage == Giving)
        break;

      struct pollfd waiting = {m_socket, POLLIN, 0};
      poll(&waiting, 1, m_surveyRunning ? 0 : kLoopGapMs);
    }

    if (g_stop)
      note("pmp-xr-host: asked to stop");

    note("pmp-xr-host: presenter stopping");
    return 0;
  }

  void Presenter::teardown()
  {
    dropStereo();
    m_ovr.stop();
  }
}

int XrPresent::run(int socketFd)
{
  struct sigaction action = {};
  action.sa_handler = onSignal;
  sigaction(SIGTERM, &action, nullptr);
  sigaction(SIGINT, &action, nullptr);
  sigaction(SIGHUP, &action, nullptr);

  Presenter presenter(socketFd);
  if (!presenter.setup())
  {
    XrProtocol::Message message = XrProtocol::make(XrProtocol::Failed);
    std::snprintf(message.reason, sizeof(message.reason), "%s",
                  "the headset runtime would not start for the panel");
    ::send(socketFd, &message, sizeof(message), MSG_NOSIGNAL);
    return 1;
  }

  return presenter.run();
}
