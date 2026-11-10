#include "Present.h"

#include "OpenVr.h"
#include "RootProperty.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

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
  const uint32_t kOpaqueFlag = VROverlayFlags_IgnoreTextureAlpha;
  const float kStereoHalves = 2.0f;
  const char kComposeName[] = "GAMESCOPE_COMPOSITE_FORCE";
  const char kComposeOurs[] = "PLEXMEDIAPLAYER_COMPOSITE_FORCE";
  bool differs(float left, float right)
  {
    return !(left > right - 0.001f && left < right + 0.001f);
  }

  float gapBetween(const float left[3], const float right[3])
  {
    float sum = 0.0f;
    for (int axis = 0; axis < 3; ++axis)
    {
      const float term = left[axis] - right[axis];
      sum += term * term;
    }
    return std::sqrt(sum);
  }

  float bowOf(const float from[3], const float middle[3], const float to[3])
  {
    float chord[3];
    for (int axis = 0; axis < 3; ++axis)
      chord[axis] = (from[axis] + to[axis]) * 0.5f;
    return gapBetween(middle, chord);
  }

  const int kScanIndexes = 128;
  const int kScanAhead = 8;
  const int kScanSlice = 512;
  const int kLoopGapMs = 50;
  const int kCheckGapMs = 1000;
  const int kSettleMs = 20000;
  const char* const kDotPrefix = "gamescope.gamescope-0.cursor.";
  const int kDotGapMs = 500;
  const int kLayerGapMs = 500;
  const int kLayerSayGapMs = 30000;
  const int kLayers = 7;
  const int kComposeWaitMs = 3000;
  const int kComposeAsks = 3;
  const int kDotSaidMax = 400;
  const float kBowCurved = 0.05f;
  const int kBowAgree = 2;
  const int kBowSayGapMs = 30000;
  const float kBowEdge = 0.98f;
  const float kArcShift = 0.01f;
  const int kArcSayGapMs = 5000;
  const int32_t kAimShiftMm = 5;
  const uint32_t kCurveShift = 2;
  const int kMarkFreshMs = 1000;
  const float kMarkHold = 0.1f;
  const int kAimSayGapMs = 1000;
  const int kDeviceSayGapMs = 500;
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

  uint32_t appidFromKey(const char* key, bool& exact)
  {
    exact = false;

    if (std::strncmp(key, kPanelPrefix, sizeof(kPanelPrefix) - 1) != 0)
      return 0;

    const char* tail = key + sizeof(kPanelPrefix) - 1;
    const char* walk = tail;
    while (*walk >= '0' && *walk <= '9')
      ++walk;

    if (walk == tail)
      return 0;

    exact = (*walk == 0);
    return static_cast<uint32_t>(strtoul(tail, nullptr, 10));
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
    bool panelPointAt(float across, float up, float out[3]) const;
    bool panelPoseAt(float across, float up, struct HmdMatrix34_t& pose) const;
    bool castAt(const float from[3], const float along[3], float hit[3], float uv[2],
                float& away) const;
    bool castAtOverlay(VROverlayHandle_t overlay, const float from[3], const float along[3],
                       float& away) const;
    float edgeAlong(const float from[3], const float along[3], const float step[3],
                    float reach) const;
    bool measureBow(float& bow, float& wide, float& tall, bool speak);
    bool measureShape(float& wide, float& tall, float& bow, bool speak);
    void setTexelAspect(float want);
    float texelWanted() const;
    float pointerWide() const;
    void tellPointer();
    void sayArc(bool atOnce);
    void takeAim();
    void sayDevices();
    void curveNow(uint32_t& radius, uint32_t& sweep, uint32_t& tall, uint32_t& by,
                  int32_t aim[3]) const;
    bool curveMoved() const;
    void probeCover();
    bool windowScaled() const;
    void askComposite();
    void dropComposite();
    void carry(int layer);
    void watchLayers(bool atOnce = false);
    VROverlayHandle_t layerHandle(int which) const;
    void fitPanel();
    void measurePanel(const char* when, bool onlyIfChanged = false);
    void probeCursor();
    void dropStereo();
    void hold();

    int m_socket = -1;
    OpenVr::Api m_ovr;

    uint32_t m_appid = 0;
    VROverlayHandle_t m_panel = k_ulOverlayHandleInvalid;
    uint32_t m_panelFlags = 0;
    bool m_panelWasStereo = false;
    float m_panelTexelAspect = 1.0f;
    float m_pointerWideTold = 0.0f;
    bool m_arcKnown = false;
    float m_arcRadius = 0.0f;
    float m_arcSweep = 0.0f;
    float m_arcWide = 0.0f;
    float m_arcTall = 0.0f;
    float m_arcLean = 0.0f;
    float m_arcLift = 0.0f;
    float m_arcSideUv[2] = {-1.0f, -1.0f};
    float m_arcSweepByUv = 0.0f;
    float m_arcCrown[3] = {0.0f, 0.0f, 0.0f};
    float m_arcRight[3] = {1.0f, 0.0f, 0.0f};
    float m_arcOver[3] = {0.0f, 1.0f, 0.0f};
    float m_arcOut[3] = {0.0f, 0.0f, 1.0f};
    float m_arcSaidRadius = -1.0f;
    float m_arcSaidSweep = -1.0f;
    float m_arcSaidWide = -1.0f;
    int64_t m_arcSaidAt = 0;
    uint32_t m_aimBy = XrProtocol::Unaimed;
    uint32_t m_aimDevice = k_unTrackedDeviceIndexInvalid;
    uint32_t m_aimNamed = k_unTrackedDeviceIndexInvalid;
    float m_aim[3] = {0.0f, 0.0f, 0.0f};
    int m_aimSaidBy = -1;
    uint32_t m_aimSaidDevice = k_unTrackedDeviceIndexInvalid;
    int64_t m_aimSaidAt = 0;
    uint32_t m_curveTold[4] = {0, 0, 0, 0};
    int32_t m_aimTold[3] = {0, 0, 0};
    uint32_t m_aimPointed = k_unTrackedDeviceIndexInvalid;
    float m_markAcross = -1.0f;
    float m_markDown = -1.0f;
    int64_t m_markAt = 0;
    int64_t m_devicesSaidAt = 0;
    float m_panelMouseWide = 0.0f;
    float m_panelMouseTall = 0.0f;
    float m_panelMetres = 0.0f;
    float m_seenCurve = -1.0f;
    float m_seenAspect = -1.0f;
    float m_seenMetres = -1.0f;
    float m_shapeHad = 0.0f;
    bool m_shapeBent = false;
    float m_shapeBow = 0.0f;
    float m_texelWant = 0.0f;
    bool m_bentSeen = false;
    int m_bentAgree = 0;
    int64_t m_bowSaidAt = 0;
    uint32_t m_packing = 0;
    int64_t m_layerAt = 0;
    int64_t m_layerSaidAt = 0;
    std::string m_layerSaid;
    VROverlayHandle_t m_layers[kLayers + 1] = {0};
    bool m_panelHidden = false;
    int m_backedSeen = -1;
    bool m_backed = false;
    int m_backedLayer = 0;
    int m_carrierLayer = 0;
    bool m_carryFailed = false;
    bool m_composeAsked = false;
    bool m_composeOurs = false;
    bool m_composeHad = false;
    uint32_t m_composeWas = 0;
    int m_composeAsks = 0;
    int64_t m_composeUntil = 0;
    uint32_t m_windowWide = 0;
    uint32_t m_windowTall = 0;
    int64_t m_coverAt = 0;
    std::vector<std::pair<VROverlayHandle_t, std::string>> m_neighbours;
    bool m_shapeFailed = false;
    bool m_texelAspectKnown = false;
    int m_relists = 0;
    int64_t m_relistAt = 0;
    bool m_stereo = false;
    bool m_told = false;
    VROverlayHandle_t m_dot = k_ulOverlayHandleInvalid;
    int m_dotSaid = 0;
    int64_t m_dotAt = 0;

    Stage m_stage = Looking;
    int m_lookTries = 0;
    int64_t m_lookAt = 0;
    int64_t m_checkAt = 0;
    int64_t m_settledAt = 0;
    bool m_settled = false;

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

    for (int which = 0; which <= kLayers; ++which)
      m_layers[which] = k_ulOverlayHandleInvalid;
    m_backedSeen = -1;
    m_backed = false;
    m_backedLayer = 0;
    m_carrierLayer = 0;
    m_carryFailed = false;
    m_layerSaid.clear();

    m_panelTexelAspect = 1.0f;
    m_texelAspectKnown = false;
    if (m_ovr.overlay->GetOverlayTexelAspect)
    {
      float was = 1.0f;
      if (m_ovr.overlay->GetOverlayTexelAspect(found.handle, &was) ==
            EVROverlayError_VROverlayError_None &&
          was > 0.0f)
      {
        m_panelTexelAspect = was;
        m_texelAspectKnown = true;
      }
    }

    note("pmp-xr-host: the Plex panel is overlay %llu \"%s\" named \"%s\", %.2f m wide, "
         "curve %.3f, texture %ux%u, %s, flags 0x%x, transform %d, pid %u",
         static_cast<unsigned long long>(found.handle), found.key, found.name, found.width,
         found.curvature, found.textureWidth, found.textureHeight,
         found.visible ? "showing" : "hidden", found.flags, found.transformType, found.pid);

    measurePanel("before any change");
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
      m_neighbours.clear();
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

        if (m_panel == k_ulOverlayHandleInvalid)
        {
          bool exact = false;
          const uint32_t keyed = appidFromKey(found.key, exact);

          if (keyed != 0 && exact)
          {
            m_appid = keyed;
            note("pmp-xr-host: this overlay carries the panel's own key");
            adopt(found);
            m_lookTries = 0;
            m_lookAt = 0;
          }
          else if (keyed != 0 && keyed != m_appid)
          {
            note("pmp-xr-host: \"%s\" is a part of the panel, not the panel; "
                 "the app is Steam id %u",
                 found.key, keyed);
            m_appid = keyed;
            if (findByKey())
            {
              m_lookTries = 0;
              m_lookAt = 0;
            }
            else
              note("pmp-xr-host: %s%u is not findable by key yet", kPanelPrefix, m_appid);
          }
        }

        if (std::strstr(found.key, ".cursor.") != nullptr ||
            std::strcmp(found.name, "Mouse Cursor") == 0)
        {
          if (m_dot != handle)
          {
            m_dot = handle;
            m_dotSaid = 0;
            note("pmp-xr-host: the mouse cursor is overlay %llu \"%s\"",
                 static_cast<unsigned long long>(handle), found.key);
          }
        }

        if (found.visible && handle != m_panel && m_neighbours.size() < 64)
          m_neighbours.push_back(std::make_pair(handle, std::string(found.key)));

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
      watchLayers(true);
      if (m_backed && m_composeOurs && m_composeAsks < 2 && nowMs() < m_composeUntil)
      {
        m_lookAt = now - kLookGapMs + kLayerGapMs;
        return;
      }

      std::string why;
      if (assertStereo(why))
      {
        m_stage = Holding;
        m_checkAt = nowMs();
        send(XrProtocol::Stereo, nullptr);
        m_told = true;
        note("pmp-xr-host: the panel is side by side, flags 0x%x", m_panelFlags);
        m_settledAt = m_checkAt;
        m_settled = false;
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
    fitPanel();
    return true;
  }

  void Presenter::measurePanel(const char* when, bool onlyIfChanged)
  {
    struct VR_IVROverlay_FnTable* ovl = m_ovr.overlay;
    if (!m_panel)
      return;

    float width = 0.0f;
    float curve = 0.0f;
    float aspect = 0.0f;
    struct HmdVector2_t scale = { { 0.0f, 0.0f } };
    struct VRTextureBounds_t bounds = { 0.0f, 0.0f, 0.0f, 0.0f };

    if (ovl->GetOverlayWidthInMeters)
      ovl->GetOverlayWidthInMeters(m_panel, &width);
    if (ovl->GetOverlayCurvature)
      ovl->GetOverlayCurvature(m_panel, &curve);
    if (ovl->GetOverlayTexelAspect)
      ovl->GetOverlayTexelAspect(m_panel, &aspect);
    if (ovl->GetOverlayMouseScale)
      ovl->GetOverlayMouseScale(m_panel, &scale);
    if (ovl->GetOverlayTextureBounds)
      ovl->GetOverlayTextureBounds(m_panel, &bounds);

    if (scale.v[0] > 0.0f && scale.v[1] > 0.0f)
    {
      m_panelMouseWide = scale.v[0];
      m_panelMouseTall = scale.v[1];
    }
    if (width > 0.0f)
      m_panelMetres = width;

    const bool changed = differs(curve, m_seenCurve) || differs(aspect, m_seenAspect) ||
                         differs(width, m_seenMetres);
    m_seenCurve = curve;
    m_seenAspect = aspect;
    m_seenMetres = width;

    if (onlyIfChanged && !changed)
      return;

    note("pmp-xr-host: the panel measures %.3f m wide, curve %.3f, aspect %.3f, "
         "mouse %.1f by %.1f, texture u %.4f to %.4f v %.4f to %.4f, %s",
         width, curve, aspect, scale.v[0], scale.v[1], bounds.uMin, bounds.uMax, bounds.vMin,
         bounds.vMax, when);
  }

  void Presenter::probeCursor()
  {
    struct VR_IVROverlay_FnTable* ovl = m_ovr.overlay;
    if (!m_panel || m_dotSaid >= kDotSaidMax)
      return;

    if (m_dot == k_ulOverlayHandleInvalid && m_appid != 0 && ovl->FindOverlay)
    {
      char key[160] = {0};
      std::snprintf(key, sizeof(key), "%s%u", kDotPrefix, m_appid);
      VROverlayHandle_t handle = k_ulOverlayHandleInvalid;
      if (ovl->FindOverlay(key, &handle) == EVROverlayError_VROverlayError_None)
      {
        m_dot = handle;
        note("pmp-xr-host: the mouse cursor is overlay %llu \"%s\", found by key",
             static_cast<unsigned long long>(handle), key);
      }
    }

    if (m_dot == k_ulOverlayHandleInvalid)
      return;
    if (!ovl->GetOverlayTransformAbsolute || !ovl->IsOverlayVisible)
      return;
    if (!ovl->IsOverlayVisible(m_dot))
      return;

    ETrackingUniverseOrigin dotOrigin = ETrackingUniverseOrigin_TrackingUniverseStanding;
    ETrackingUniverseOrigin panelOrigin = ETrackingUniverseOrigin_TrackingUniverseStanding;
    struct HmdMatrix34_t dotPose;
    struct HmdMatrix34_t panelPose;

    if (ovl->GetOverlayTransformAbsolute(m_dot, &dotOrigin, &dotPose) !=
          EVROverlayError_VROverlayError_None ||
        ovl->GetOverlayTransformAbsolute(m_panel, &panelOrigin, &panelPose) !=
          EVROverlayError_VROverlayError_None)
      return;

    float gap[3];
    for (int axis = 0; axis < 3; ++axis)
      gap[axis] = dotPose.m[axis][3] - panelPose.m[axis][3];

    float local[3] = { 0.0f, 0.0f, 0.0f };
    for (int axis = 0; axis < 3; ++axis)
      for (int term = 0; term < 3; ++term)
        local[axis] += panelPose.m[term][axis] * gap[term];

    float width = 0.0f;
    float aspect = 0.0f;
    if (ovl->GetOverlayWidthInMeters)
      ovl->GetOverlayWidthInMeters(m_panel, &width);
    if (ovl->GetOverlayTexelAspect)
      ovl->GetOverlayTexelAspect(m_panel, &aspect);

    ++m_dotSaid;
    note("pmp-xr-host: the cursor sits at panel x %.4f y %.4f z %.4f m, panel %.3f m wide, "
         "aspect %.3f, origins %d and %d",
         local[0], local[1], local[2], width, aspect, static_cast<int>(dotOrigin),
         static_cast<int>(panelOrigin));
  }


  bool Presenter::panelPointAt(float across, float up, float out[3]) const
  {
    struct VR_IVROverlay_FnTable* ovl = m_ovr.overlay;
    if (!ovl || !ovl->GetTransformForOverlayCoordinates)
      return false;

    struct HmdVector2_t at;
    at.v[0] = across;
    at.v[1] = up;

    struct HmdMatrix34_t pose;
    std::memset(&pose, 0, sizeof(pose));

    const EVROverlayError error = ovl->GetTransformForOverlayCoordinates(
      m_panel, ETrackingUniverseOrigin_TrackingUniverseStanding, at, &pose);
    if (error != EVROverlayError_VROverlayError_None)
    {
      note("pmp-xr-host: the headset will not place panel point %.2f %.2f: %s", across, up,
           errorName(error));
      return false;
    }

    out[0] = pose.m[0][3];
    out[1] = pose.m[1][3];
    out[2] = pose.m[2][3];
    return true;
  }

  bool Presenter::panelPoseAt(float across, float up, struct HmdMatrix34_t& pose) const
  {
    struct VR_IVROverlay_FnTable* ovl = m_ovr.overlay;
    if (!ovl || !ovl->GetTransformForOverlayCoordinates)
      return false;

    struct HmdVector2_t at;
    at.v[0] = across;
    at.v[1] = up;
    std::memset(&pose, 0, sizeof(pose));

    return ovl->GetTransformForOverlayCoordinates(
             m_panel, ETrackingUniverseOrigin_TrackingUniverseStanding, at, &pose) ==
           EVROverlayError_VROverlayError_None;
  }

  bool Presenter::castAt(const float from[3], const float along[3], float hit[3], float uv[2],
                         float& away) const
  {
    struct VR_IVROverlay_FnTable* ovl = m_ovr.overlay;
    if (!ovl || !ovl->ComputeOverlayIntersection)
      return false;

    struct VROverlayIntersectionParams_t ask;
    std::memset(&ask, 0, sizeof(ask));
    for (int axis = 0; axis < 3; ++axis)
    {
      ask.vSource.v[axis] = from[axis];
      ask.vDirection.v[axis] = along[axis];
    }
    ask.eOrigin = ETrackingUniverseOrigin_TrackingUniverseStanding;

    struct VROverlayIntersectionResults_t got;
    std::memset(&got, 0, sizeof(got));

    if (!ovl->ComputeOverlayIntersection(m_panel, &ask, &got))
      return false;

    for (int axis = 0; axis < 3; ++axis)
      hit[axis] = got.vPoint.v[axis];
    uv[0] = got.vUVs.v[0];
    uv[1] = got.vUVs.v[1];
    away = got.fDistance;
    return true;
  }

  bool Presenter::castAtOverlay(VROverlayHandle_t overlay, const float from[3],
                                const float along[3], float& away) const
  {
    struct VR_IVROverlay_FnTable* ovl = m_ovr.overlay;
    if (!ovl || !ovl->ComputeOverlayIntersection || overlay == k_ulOverlayHandleInvalid)
      return false;

    struct VROverlayIntersectionParams_t ask;
    std::memset(&ask, 0, sizeof(ask));
    for (int axis = 0; axis < 3; ++axis)
    {
      ask.vSource.v[axis] = from[axis];
      ask.vDirection.v[axis] = along[axis];
    }
    ask.eOrigin = ETrackingUniverseOrigin_TrackingUniverseStanding;

    struct VROverlayIntersectionResults_t got;
    std::memset(&got, 0, sizeof(got));

    if (!ovl->ComputeOverlayIntersection(overlay, &ask, &got))
      return false;

    away = got.fDistance;
    return true;
  }

  float Presenter::edgeAlong(const float from[3], const float along[3], const float step[3],
                             float reach) const
  {
    float hit[3];
    float uv[2];
    float away = 0.0f;
    float start[3];

    for (int axis = 0; axis < 3; ++axis)
      start[axis] = from[axis] + step[axis] * reach;
    if (castAt(start, along, hit, uv, away))
      return reach;

    float near = 0.0f;
    float far = reach;
    for (int tries = 0; tries < 20; ++tries)
    {
      const float middle = (near + far) * 0.5f;
      for (int axis = 0; axis < 3; ++axis)
        start[axis] = from[axis] + step[axis] * middle;
      if (castAt(start, along, hit, uv, away))
        near = middle;
      else
        far = middle;
    }
    return near;
  }

  bool Presenter::measureBow(float& bow, float& wide, float& tall, bool speak)
  {
    struct VR_IVROverlay_FnTable* ovl = m_ovr.overlay;
    if (!ovl || !ovl->ComputeOverlayIntersection)
      return false;

    float corner[3];
    float unit[3];
    if (!panelPointAt(0.0f, 0.0f, corner) || !panelPointAt(1.0f, 0.0f, unit))
      return false;

    float across = 1.0f;
    float up = 1.0f;
    if (m_panelMetres > 0.0f && gapBetween(corner, unit) < m_panelMetres * 0.5f &&
        m_panelMouseWide > 0.0f && m_panelMouseTall > 0.0f)
    {
      across = m_panelMouseWide;
      up = m_panelMouseTall;
    }

    struct HmdMatrix34_t pose;
    if (!panelPoseAt(across * 0.5f, up * 0.5f, pose))
      return false;

    float right[3];
    float over[3];
    float back[3];
    float middle[3];
    for (int axis = 0; axis < 3; ++axis)
    {
      right[axis] = pose.m[axis][0];
      over[axis] = pose.m[axis][1];
      back[axis] = pose.m[axis][2];
      middle[axis] = pose.m[axis][3];
    }

    const float standoff = 2.0f;
    float from[3];
    float along[3];
    float hit[3];
    float uv[2] = { -1.0f, -1.0f };
    float away = 0.0f;
    bool reached = false;

    for (int facing = 0; facing < 2 && !reached; ++facing)
    {
      const float sign = facing == 0 ? 1.0f : -1.0f;
      for (int axis = 0; axis < 3; ++axis)
      {
        from[axis] = middle[axis] + back[axis] * standoff * sign;
        along[axis] = -back[axis] * sign;
      }
      reached = castAt(from, along, hit, uv, away);
    }

    if (!reached)
      return false;

    float step[3];
    const float reach = 4.0f;

    for (int axis = 0; axis < 3; ++axis)
      step[axis] = -right[axis];
    const float toLeft = edgeAlong(from, along, step, reach);

    for (int axis = 0; axis < 3; ++axis)
      step[axis] = right[axis];
    const float toRight = edgeAlong(from, along, step, reach);

    for (int axis = 0; axis < 3; ++axis)
      step[axis] = -over[axis];
    const float toBase = edgeAlong(from, along, step, reach);

    for (int axis = 0; axis < 3; ++axis)
      step[axis] = over[axis];
    const float toTop = edgeAlong(from, along, step, reach);

    float leftUv[2] = { -1.0f, -1.0f };
    float rightUv[2] = { -1.0f, -1.0f };
    float edgeHit[3];
    float leftAway = away;
    float rightAway = away;
    float at[3];

    for (int axis = 0; axis < 3; ++axis)
      at[axis] = from[axis] - right[axis] * toLeft * kBowEdge;
    castAt(at, along, edgeHit, leftUv, leftAway);

    for (int axis = 0; axis < 3; ++axis)
      at[axis] = from[axis] + right[axis] * toRight * kBowEdge;
    castAt(at, along, edgeHit, rightUv, rightAway);

    wide = toLeft + toRight;
    tall = toBase + toTop;
    const float edgeAway = (leftAway + rightAway) * 0.5f;
    bow = edgeAway - away;

    const float lean = (toRight - toLeft) * 0.5f;
    const float lift = (toTop - toBase) * 0.5f;
    float crown[3];
    float crownUv[2] = { -1.0f, -1.0f };
    float crownAway = away;

    for (int axis = 0; axis < 3; ++axis)
      at[axis] = from[axis] + right[axis] * lean + over[axis] * lift;
    if (!castAt(at, along, crown, crownUv, crownAway))
    {
      crownAway = away;
      for (int axis = 0; axis < 3; ++axis)
        crown[axis] = hit[axis] + right[axis] * lean + over[axis] * lift;
    }

    const float leftOut = toLeft * kBowEdge + lean;
    const float rightOut = toRight * kBowEdge - lean;
    const float leftRise = crownAway - leftAway;
    const float rightRise = crownAway - rightAway;
    const float sideOut = (leftOut + rightOut) * 0.5f;
    const float rise = (leftRise + rightRise) * 0.5f;
    float facing = rise < 0.0f ? -1.0f : 1.0f;

    struct VR_IVRSystem_FnTable* sys = m_ovr.system;
    if (!(std::fabs(rise) > kBowCurved) && sys && sys->GetDeviceToAbsoluteTrackingPose)
    {
      struct TrackedDevicePose_t head;
      std::memset(&head, 0, sizeof(head));
      sys->GetDeviceToAbsoluteTrackingPose(ETrackingUniverseOrigin_TrackingUniverseStanding, 0.0f,
                                           &head, 1);
      if (head.bPoseIsValid)
      {
        float before = 0.0f;
        for (int axis = 0; axis < 3; ++axis)
          before += (head.mDeviceToAbsoluteTracking.m[axis][3] - crown[axis]) * -along[axis];
        facing = before < 0.0f ? -1.0f : 1.0f;
      }
    }

    float radius = 0.0f;
    float sweep = 0.0f;
    float sweepByUv = 0.0f;
    if (std::fabs(rise) > kBowCurved && sideOut > 0.0f)
    {
      radius = (sideOut * sideOut + rise * rise) / (2.0f * std::fabs(rise));
      sweep = 2.0f * std::asin(std::min(1.0f, wide * 0.5f / radius));

      const float uvSpan = std::fabs(rightUv[0] - leftUv[0]);
      if (uvSpan > 0.01f)
        sweepByUv = 2.0f * (std::atan2(std::fabs(leftRise), leftOut) +
                            std::atan2(std::fabs(rightRise), rightOut)) / uvSpan;
    }

    const float turn =
      leftUv[0] >= 0.0f && rightUv[0] >= 0.0f && rightUv[0] < leftUv[0] ? -1.0f : 1.0f;

    m_arcKnown = true;
    m_arcRadius = radius;
    m_arcSweep = sweep;
    m_arcWide = wide;
    m_arcTall = tall;
    m_arcLean = lean * turn;
    m_arcLift = lift;
    m_arcSideUv[0] = leftUv[0];
    m_arcSideUv[1] = rightUv[0];
    m_arcSweepByUv = sweepByUv;
    for (int axis = 0; axis < 3; ++axis)
    {
      m_arcCrown[axis] = crown[axis];
      m_arcRight[axis] = right[axis] * turn;
      m_arcOut[axis] = -along[axis] * facing;
    }
    m_arcOver[0] = m_arcOut[1] * m_arcRight[2] - m_arcOut[2] * m_arcRight[1];
    m_arcOver[1] = m_arcOut[2] * m_arcRight[0] - m_arcOut[0] * m_arcRight[2];
    m_arcOver[2] = m_arcOut[0] * m_arcRight[1] - m_arcOut[1] * m_arcRight[0];
    if (over[0] * m_arcOver[0] + over[1] * m_arcOver[1] + over[2] * m_arcOver[2] < 0.0f)
      m_arcLift = -lift;

    if (speak)
      note("pmp-xr-host: the panel the headset can touch is %.3f by %.3f m, %.3f m away in the "
           "middle and %.3f m at the edges, bowing %.4f m",
           wide, tall, away, edgeAway, bow);
    return true;
  }

  void Presenter::setTexelAspect(float want)
  {
    struct VR_IVROverlay_FnTable* ovl = m_ovr.overlay;
    if (!ovl || !ovl->SetOverlayTexelAspect)
      return;

    const EVROverlayError error = ovl->SetOverlayTexelAspect(m_panel, want);
    if (error == EVROverlayError_VROverlayError_None)
      m_panelTexelAspect = want;

    note("pmp-xr-host: the panel is %.3f texels wide per tall%s", want,
         error == EVROverlayError_VROverlayError_None ? "" : " (the runtime complained)");
  }

  float Presenter::texelWanted() const
  {
    if (m_packing != XrProtocol::SideBySide && m_packing != XrProtocol::OverUnder)
      return 1.0f;
    if (!(m_panelMouseWide > 0.0f) || !(m_panelMouseTall > 0.0f))
      return 1.0f;
    if (m_windowWide < 2 || m_windowTall < 2)
      return 1.0f;

    if (m_backed || m_carrierLayer != 0)
      return 1.0f;

    return m_packing == XrProtocol::SideBySide ? 2.0f : 0.5f;
  }

  float Presenter::pointerWide() const
  {
    return m_stereo && m_carrierLayer == 0 && !m_backed ? kStereoHalves : 1.0f;
  }

  void Presenter::sayArc(bool atOnce)
  {
    if (!m_arcKnown)
      return;

    const int64_t now = nowMs();
    const bool shifted =
      std::fabs(m_arcRadius - m_arcSaidRadius) > kArcShift * std::max(m_arcRadius, 1.0f) ||
      std::fabs(m_arcSweep - m_arcSaidSweep) > kArcShift * std::max(m_arcSweep, 1.0f) ||
      std::fabs(m_arcWide - m_arcSaidWide) > kArcShift * std::max(m_arcWide, 1.0f);
    if (!atOnce && (!shifted || now - m_arcSaidAt < kArcSayGapMs))
      return;

    m_arcSaidAt = now;
    m_arcSaidRadius = m_arcRadius;
    m_arcSaidSweep = m_arcSweep;
    m_arcSaidWide = m_arcWide;

    const float degrees = 57.29578f;
    if (m_arcRadius > 0.0f)
      note("pmp-xr-host: the surface the laser lands on is an arc of radius %.3f m sweeping "
           "%.1f degrees, %.3f m along the arc, %.3f m across and %.3f m tall, its middle "
           "%.3f m right and %.3f m above the panel's own; the texture runs from %.4f to %.4f "
           "between the outermost rays, which makes the sweep %.1f degrees",
           m_arcRadius, m_arcSweep * degrees, m_arcRadius * m_arcSweep, m_arcWide, m_arcTall,
           m_arcLean, m_arcLift, m_arcSideUv[0], m_arcSideUv[1], m_arcSweepByUv * degrees);
    else
      note("pmp-xr-host: the surface the laser lands on is flat, %.3f by %.3f m, its middle "
           "%.3f m right and %.3f m above the panel's own; the texture runs from %.4f to %.4f "
           "between the outermost rays",
           m_arcWide, m_arcTall, m_arcLean, m_arcLift, m_arcSideUv[0], m_arcSideUv[1]);
  }

  void Presenter::takeAim()
  {
    m_aimBy = XrProtocol::Unaimed;
    m_aimDevice = k_unTrackedDeviceIndexInvalid;
    m_aimNamed = k_unTrackedDeviceIndexInvalid;

    if (!m_stereo || !m_arcKnown || !(m_arcRadius > 0.0f) || !(pointerWide() > 1.0f))
    {
      m_aimSaidBy = -1;
      return;
    }

    struct VR_IVRSystem_FnTable* sys = m_ovr.system;
    struct VR_IVROverlay_FnTable* ovl = m_ovr.overlay;

    struct TrackedDevicePose_t poses[k_unMaxTrackedDeviceCount];
    std::memset(poses, 0, sizeof(poses));
    const bool posed = sys && sys->GetDeviceToAbsoluteTrackingPose;
    if (posed)
      sys->GetDeviceToAbsoluteTrackingPose(ETrackingUniverseOrigin_TrackingUniverseStanding,
                                           0.0f, poses, k_unMaxTrackedDeviceCount);

    if (ovl && ovl->GetPrimaryDashboardDevice)
      m_aimNamed = ovl->GetPrimaryDashboardDevice();

    if (posed && m_aimNamed < k_unMaxTrackedDeviceCount && poses[m_aimNamed].bPoseIsValid)
    {
      m_aimBy = XrProtocol::DashboardDevice;
      m_aimDevice = m_aimNamed;
    }
    else if (posed && sys->GetTrackedDeviceClass)
    {
      const bool marked = m_markAt > 0 && nowMs() - m_markAt < kMarkFreshMs;
      const bool kept = !marked && m_aimPointed < k_unMaxTrackedDeviceCount &&
                        poses[m_aimPointed].bPoseIsValid &&
                        sys->GetTrackedDeviceClass(m_aimPointed) ==
                          ETrackedDeviceClass_TrackedDeviceClass_Controller;
      float best = -2.0f;
      float nearest = -1.0f;
      for (uint32_t device = 0; device < k_unMaxTrackedDeviceCount; ++device)
      {
        if (!poses[device].bPoseIsValid ||
            sys->GetTrackedDeviceClass(device) != ETrackedDeviceClass_TrackedDeviceClass_Controller)
          continue;

        const struct HmdMatrix34_t& held = poses[device].mDeviceToAbsoluteTracking;
        float toCrown[3];
        float length = 0.0f;
        for (int axis = 0; axis < 3; ++axis)
        {
          toCrown[axis] = m_arcCrown[axis] - held.m[axis][3];
          length += toCrown[axis] * toCrown[axis];
        }
        length = std::sqrt(length);
        if (!(length > 0.0f))
          continue;

        float facing = 0.0f;
        for (int axis = 0; axis < 3; ++axis)
          facing += -held.m[axis][2] * toCrown[axis] / length;

        float gap = -1.0f;
        if (marked)
        {
          float from[3];
          float forward[3];
          float hit[3];
          float uv[2] = { -1.0f, -1.0f };
          float away = 0.0f;
          for (int axis = 0; axis < 3; ++axis)
          {
            from[axis] = held.m[axis][3];
            forward[axis] = -held.m[axis][2];
          }
          if (castAt(from, forward, hit, uv, away))
          {
            gap = std::fabs(uv[0] - m_markAcross);
            if (device == m_aimPointed)
              gap = std::max(0.0f, gap - kMarkHold);
          }
        }

        const bool closer = gap >= 0.0f && (nearest < 0.0f || gap < nearest);
        if (closer || (nearest < 0.0f && facing > best))
        {
          if (closer)
            nearest = gap;
          best = facing;
          m_aimBy = closer ? XrProtocol::PointedController : XrProtocol::AimedController;
          m_aimDevice = device;
        }
      }

      if (kept)
      {
        m_aimBy = XrProtocol::PointedController;
        m_aimDevice = m_aimPointed;
      }
      else if (m_aimBy == XrProtocol::PointedController)
        m_aimPointed = m_aimDevice;
    }

    if (m_aimBy != XrProtocol::Unaimed)
    {
      const struct HmdMatrix34_t& held = poses[m_aimDevice].mDeviceToAbsoluteTracking;
      float aim[3] = {0.0f, 0.0f, 0.0f};
      for (int axis = 0; axis < 3; ++axis)
      {
        const float gap = held.m[axis][3] - m_arcCrown[axis];
        aim[0] += gap * m_arcRight[axis];
        aim[1] += gap * m_arcOver[axis];
        aim[2] += gap * m_arcOut[axis];
      }
      for (int axis = 0; axis < 3; ++axis)
        m_aim[axis] = aim[axis];
    }

    const int64_t now = nowMs();
    const bool same = int(m_aimBy) == m_aimSaidBy && m_aimDevice == m_aimSaidDevice;
    if (now - m_aimSaidAt < (same ? kBowSayGapMs : kAimSayGapMs))
      return;

    m_aimSaidBy = int(m_aimBy);
    m_aimSaidDevice = m_aimDevice;
    m_aimSaidAt = now;

    if (m_aimBy == XrProtocol::Unaimed)
      note("pmp-xr-host: nothing says where the laser starts (the dashboard names %s, poses "
           "%s), so the pointer cannot follow it onto the picture",
           m_aimNamed == k_unTrackedDeviceIndexInvalid ? "no device" : "a device with no pose",
           posed ? "were read" : "cannot be read");
    else
      note("pmp-xr-host: the laser starts at device %u, %s%s, %.3f m right, %.3f m up and "
           "%.3f m out from the middle of the surface it lands on",
           m_aimDevice,
           m_aimBy == XrProtocol::DashboardDevice
             ? "the one the dashboard names"
             : m_aimBy == XrProtocol::PointedController
                 ? "the controller whose ray lands nearest the pointer"
                 : "the controller pointed nearest the panel",
           m_aimBy == XrProtocol::DashboardDevice
             ? ""
             : m_aimNamed == k_unTrackedDeviceIndexInvalid
                 ? ", as the dashboard names no device"
                 : ", as the dashboard's device has no pose",
           m_aim[0], m_aim[1], m_aim[2]);
  }

  void Presenter::sayDevices()
  {
    const int64_t now = nowMs();
    if (now - m_devicesSaidAt < kDeviceSayGapMs)
      return;
    m_devicesSaidAt = now;

    struct VR_IVRSystem_FnTable* sys = m_ovr.system;
    struct VR_IVROverlay_FnTable* ovl = m_ovr.overlay;

    if (!sys || !sys->GetDeviceToAbsoluteTrackingPose || !sys->GetTrackedDeviceClass)
    {
      note("pmp-xr-host: with the pointer %.4f across and %.4f down the window, no device "
           "poses can be read",
           m_markAcross, m_markDown);
      return;
    }

    struct TrackedDevicePose_t poses[k_unMaxTrackedDeviceCount];
    std::memset(poses, 0, sizeof(poses));
    sys->GetDeviceToAbsoluteTrackingPose(ETrackingUniverseOrigin_TrackingUniverseStanding, 0.0f,
                                         poses, k_unMaxTrackedDeviceCount);

    const uint32_t named = ovl && ovl->GetPrimaryDashboardDevice
                             ? ovl->GetPrimaryDashboardDevice()
                             : k_unTrackedDeviceIndexInvalid;

    int said = 0;
    for (uint32_t device = 0; device < k_unMaxTrackedDeviceCount; ++device)
    {
      if (!poses[device].bPoseIsValid)
        continue;

      const ETrackedDeviceClass kind = sys->GetTrackedDeviceClass(device);
      if (kind != ETrackedDeviceClass_TrackedDeviceClass_HMD &&
          kind != ETrackedDeviceClass_TrackedDeviceClass_Controller)
        continue;

      const struct HmdMatrix34_t& held = poses[device].mDeviceToAbsoluteTracking;
      float from[3];
      float forward[3];
      float placed[3] = {0.0f, 0.0f, 0.0f};
      for (int axis = 0; axis < 3; ++axis)
      {
        from[axis] = held.m[axis][3];
        forward[axis] = -held.m[axis][2];
        const float gap = held.m[axis][3] - m_arcCrown[axis];
        placed[0] += gap * m_arcRight[axis];
        placed[1] += gap * m_arcOver[axis];
        placed[2] += gap * m_arcOut[axis];
      }

      char where[128];
      if (m_arcKnown)
        std::snprintf(where, sizeof(where),
                      "%.3f m right, %.3f m up and %.3f m out from the middle of the surface "
                      "the laser lands on",
                      placed[0], placed[1], placed[2]);
      else
        std::snprintf(where, sizeof(where), "somewhere about a surface not yet measured");

      float hit[3];
      float uv[2] = { -1.0f, -1.0f };
      float away = 0.0f;
      char lands[96];
      if (castAt(from, forward, hit, uv, away))
        std::snprintf(lands, sizeof(lands), "lands on the texture at %.4f, %.4f, %.3f m along",
                      uv[0], uv[1], away);
      else
        std::snprintf(lands, sizeof(lands), "misses the surface");

      const bool taken = m_aimBy != XrProtocol::Unaimed && device == m_aimDevice;
      note("pmp-xr-host: with the pointer %.4f across and %.4f down the window, device %u, "
           "%s%s, is %s and its forward ray %s",
           m_markAcross, m_markDown, device,
           kind == ETrackedDeviceClass_TrackedDeviceClass_HMD ? "the head" : "a controller",
           device == named && taken ? " (named by the dashboard and taken as the laser's start)"
             : device == named ? " (named by the dashboard)"
             : taken ? " (taken as the laser's start)"
             : "",
           where, lands);
      ++said;
    }

    if (said == 0)
      note("pmp-xr-host: with the pointer %.4f across and %.4f down the window, no head or "
           "controller has a pose (the dashboard names %s)",
           m_markAcross, m_markDown,
           named == k_unTrackedDeviceIndexInvalid ? "no device" : "a device");
  }

  void Presenter::curveNow(uint32_t& radius, uint32_t& sweep, uint32_t& tall, uint32_t& by,
                           int32_t aim[3]) const
  {
    radius = 0;
    sweep = 0;
    tall = 0;
    by = XrProtocol::Unaimed;
    aim[0] = aim[1] = aim[2] = 0;

    if (!m_stereo || !m_arcKnown || !(m_arcRadius > 0.0f) || !(pointerWide() > 1.0f))
      return;

    radius = static_cast<uint32_t>(m_arcRadius * 1000.0f + 0.5f);
    sweep = static_cast<uint32_t>(m_arcSweep * 1000.0f + 0.5f);
    tall = static_cast<uint32_t>(m_arcTall * 1000.0f + 0.5f);
    by = m_aimBy;
    if (by != XrProtocol::Unaimed)
      for (int axis = 0; axis < 3; ++axis)
        aim[axis] = static_cast<int32_t>(std::lround(m_aim[axis] * 1000.0f));
  }

  bool Presenter::curveMoved() const
  {
    uint32_t radius = 0;
    uint32_t sweep = 0;
    uint32_t tall = 0;
    uint32_t by = 0;
    int32_t aim[3];
    curveNow(radius, sweep, tall, by, aim);

    const uint32_t now[3] = {radius, sweep, tall};
    if (by != m_curveTold[3])
      return true;
    for (int which = 0; which < 3; ++which)
    {
      const uint32_t gap =
        now[which] > m_curveTold[which] ? now[which] - m_curveTold[which]
                                        : m_curveTold[which] - now[which];
      if (gap >= kCurveShift || (now[which] == 0) != (m_curveTold[which] == 0))
        return true;
    }
    for (int axis = 0; axis < 3; ++axis)
      if (std::abs(aim[axis] - m_aimTold[axis]) >= kAimShiftMm)
        return true;
    return false;
  }

  void Presenter::tellPointer()
  {
    if (!m_told || m_stage != Holding)
      return;

    if (pointerWide() != m_pointerWideTold)
    {
      send(XrProtocol::Stereo, nullptr);
      return;
    }

    takeAim();
    if (curveMoved())
      send(XrProtocol::Pointer, nullptr);
  }

  VROverlayHandle_t Presenter::layerHandle(int which) const
  {
    struct VR_IVROverlay_FnTable* ovl = m_ovr.overlay;
    if (!ovl || !ovl->FindOverlay || m_appid == 0)
      return k_ulOverlayHandleInvalid;

    char key[192];
    std::snprintf(key, sizeof(key), "%s%u.layer%d", kPanelPrefix, m_appid, which);

    VROverlayHandle_t handle = k_ulOverlayHandleInvalid;
    if (ovl->FindOverlay(key, &handle) != EVROverlayError_VROverlayError_None)
      return k_ulOverlayHandleInvalid;

    return handle;
  }

  bool Presenter::windowScaled() const
  {
    if (!(m_panelMouseWide > 0.0f) || !(m_panelMouseTall > 0.0f))
      return false;
    if (m_windowWide < 2 || m_windowTall < 2)
      return false;

    return std::fabs(m_panelMouseWide - static_cast<float>(m_windowWide)) > 1.0f ||
           std::fabs(m_panelMouseTall - static_cast<float>(m_windowTall)) > 1.0f;
  }

  void Presenter::askComposite()
  {
    const bool again = m_composeOurs;
    m_composeAsked = true;

    if (!again)
    {
      const RootProperty::Reading was = RootProperty::read(kComposeName);
      if (!was.reached)
      {
        note("pmp-xr-host: %s cannot be read, %s, so the compositor is left as it is",
             kComposeName, was.trouble.c_str());
        return;
      }

      m_composeHad = was.present;
      m_composeWas = was.value;
      RootProperty::write(kComposeOurs, 1);
    }

    ++m_composeAsks;
    m_composeUntil = nowMs() + kComposeWaitMs;

    const RootProperty::Reading now = RootProperty::write(kComposeName, 1);
    if (!now.reached || !now.present || now.value != 1)
    {
      note("pmp-xr-host: %s would not be set, %s", kComposeName,
           now.reached ? "it does not read back" : now.trouble.c_str());
      if (!again)
        RootProperty::remove(kComposeOurs);
      return;
    }

    m_composeOurs = true;

    if (again)
      note("pmp-xr-host: %s was written again, time %d of %d, and reads %u", kComposeName,
           m_composeAsks, kComposeAsks, now.value);
    else if (m_composeHad)
      note("pmp-xr-host: %s was %u and now reads %u, so the compositor is asked to draw the "
           "window onto the panel",
           kComposeName, m_composeWas, now.value);
    else
      note("pmp-xr-host: %s was absent and now reads %u, so the compositor is asked to draw the "
           "window onto the panel",
           kComposeName, now.value);
  }

  void Presenter::dropComposite()
  {
    if (!m_composeOurs)
      return;
    m_composeOurs = false;

    const RootProperty::Reading now = m_composeHad
                                        ? RootProperty::write(kComposeName, m_composeWas)
                                        : RootProperty::remove(kComposeName);
    RootProperty::remove(kComposeOurs);

    if (!now.reached)
      note("pmp-xr-host: %s could not be put back, %s", kComposeName, now.trouble.c_str());
    else if (now.present)
      note("pmp-xr-host: %s was put back and reads %u", kComposeName, now.value);
    else
      note("pmp-xr-host: %s was taken away again", kComposeName);
  }

  void Presenter::carry(int layer)
  {
    struct VR_IVROverlay_FnTable* ovl = m_ovr.overlay;
    if (!ovl || !ovl->SetOverlayFlag || !ovl->GetOverlayFlag || layer == m_carrierLayer)
      return;

    const VROverlayHandle_t from = m_carrierLayer != 0 ? m_layers[m_carrierLayer] : m_panel;
    const VROverlayHandle_t to = layer != 0 ? m_layers[layer] : m_panel;
    if (to == k_ulOverlayHandleInvalid)
      return;

    const EVROverlayError set = ovl->SetOverlayFlag(to, VROverlayFlags_SideBySide_Parallel, true);

    bool enabled = false;
    const EVROverlayError read =
      ovl->GetOverlayFlag(to, VROverlayFlags_SideBySide_Parallel, &enabled);

    if (set != EVROverlayError_VROverlayError_None ||
        read != EVROverlayError_VROverlayError_None || !enabled)
    {
      m_carryFailed = true;
      ovl->SetOverlayFlag(to, VROverlayFlags_SideBySide_Parallel, false);
      if (layer != 0)
        note("pmp-xr-host: layer %d would not take the side by side flag (%s), so it stays "
             "where it was",
             layer, errorName(set));
      else
        note("pmp-xr-host: the panel would not take the side by side flag back (%s)",
             errorName(set));
      return;
    }

    const bool keep = m_carrierLayer == 0 && m_panelWasStereo;
    const char* left = "was left where it had been";
    if (from != k_ulOverlayHandleInvalid && !keep)
    {
      const EVROverlayError cleared =
        ovl->SetOverlayFlag(from, VROverlayFlags_SideBySide_Parallel, false);
      left = cleared == EVROverlayError_VROverlayError_None
               ? "was cleared where it had been"
               : "would not clear where it had been";
    }

    m_carrierLayer = layer;

    if (ovl->GetOverlayFlags)
      ovl->GetOverlayFlags(m_panel, &m_panelFlags);

    if (layer != 0)
      note("pmp-xr-host: the side by side flag now sits on layer %d, which holds the picture, "
           "and %s",
           layer, left);
    else
      note("pmp-xr-host: the side by side flag now sits on the panel again, and %s", left);
  }

  void Presenter::watchLayers(bool atOnce)
  {
    struct VR_IVROverlay_FnTable* ovl = m_ovr.overlay;
    if (!ovl || !ovl->FindOverlay || !ovl->IsOverlayVisible || !ovl->GetOverlayFlags ||
        m_appid == 0 || m_panel == k_ulOverlayHandleInvalid)
      return;

    const int64_t now = nowMs();
    if (!atOnce && m_layerAt != 0 && now - m_layerAt < kLayerGapMs)
      return;
    m_layerAt = now;

    const bool hidden = !ovl->IsOverlayVisible(m_panel);
    if (hidden != m_panelHidden)
    {
      m_panelHidden = hidden;
      note("pmp-xr-host: the panel is %s, so the layers are %s", hidden ? "hidden" : "showing",
           hidden ? "left unread" : "read");
    }
    if (hidden)
      return;

    int lowest = 0;
    uint32_t lowestFlags = 0;
    std::string showing;

    for (int which = 1; which <= kLayers; ++which)
    {
      if (m_layers[which] == k_ulOverlayHandleInvalid)
        m_layers[which] = layerHandle(which);

      const VROverlayHandle_t handle = m_layers[which];
      if (handle == k_ulOverlayHandleInvalid)
        continue;

      uint32_t flags = 0;
      if (ovl->GetOverlayFlags(handle, &flags) != EVROverlayError_VROverlayError_None)
      {
        m_layers[which] = k_ulOverlayHandleInvalid;
        continue;
      }

      if (!ovl->IsOverlayVisible(handle))
        continue;

      char piece[64];
      std::snprintf(piece, sizeof(piece), " layer %d flags 0x%x,", which, flags);
      showing += piece;

      if (lowest == 0)
      {
        lowest = which;
        lowestFlags = flags;
      }
    }

    const bool backed = lowest != 0 && (lowestFlags & kOpaqueFlag) != 0 && windowScaled();
    m_backed = backed;
    m_backedLayer = backed ? lowest : 0;

    const int carriedBy = m_carrierLayer;
    const bool turned = m_backedSeen != (backed ? 1 : 0);
    if (turned)
    {
      m_backedSeen = backed ? 1 : 0;
      if (backed)
        note("pmp-xr-host: the compositor draws the %u by %u window on layer %d and leaves the "
             "%.0f by %.0f panel black behind it",
             m_windowWide, m_windowTall, lowest, m_panelMouseWide, m_panelMouseTall);
      else
        note("pmp-xr-host: the compositor draws the %u by %u window on the %.0f by %.0f panel "
             "itself",
             m_windowWide, m_windowTall, m_panelMouseWide, m_panelMouseTall);
    }

    if (backed && !m_composeAsked)
      askComposite();

    if (m_stereo && !m_carryFailed)
    {
      const bool waiting = m_composeOurs && now < m_composeUntil;

      if (backed && m_carrierLayer != 0 && m_carrierLayer != lowest)
        carry(lowest);
      else if (!backed && m_carrierLayer != 0)
        carry(0);
      else if (backed && m_carrierLayer == 0 && !waiting)
      {
        if (m_composeOurs && m_composeAsks < kComposeAsks)
          askComposite();
        else
        {
          if (m_composeOurs)
            note("pmp-xr-host: the compositor still draws the window on layer %d after %s was "
                 "written %d times",
                 lowest, kComposeName, m_composeAsks);
          dropComposite();
          carry(lowest);
        }
      }
    }

    if (m_stereo && (turned || carriedBy != m_carrierLayer))
      fitPanel();

    char carrier[32];
    if (!m_stereo)
      std::snprintf(carrier, sizeof(carrier), "nothing");
    else if (m_carrierLayer != 0)
      std::snprintf(carrier, sizeof(carrier), "layer %d", m_carrierLayer);
    else
      std::snprintf(carrier, sizeof(carrier), "the panel");

    char head[320];
    std::snprintf(head, sizeof(head), "window %u by %u, plane %.0f by %.0f, showing%s side by "
                  "side on %s, texel aspect %.3f",
                  m_windowWide, m_windowTall, m_panelMouseWide, m_panelMouseTall,
                  showing.empty() ? " no layers," : showing.c_str(), carrier,
                  m_panelTexelAspect);

    if (m_layerSaid == head && now - m_layerSaidAt < kLayerSayGapMs)
      return;
    m_layerSaid = head;
    m_layerSaidAt = now;

    const RootProperty::Reading reads = RootProperty::read(kComposeName);
    if (!reads.reached)
      note("pmp-xr-host: layers: %s, %s cannot be read, %s", head, kComposeName,
           reads.trouble.c_str());
    else if (reads.present)
      note("pmp-xr-host: layers: %s, %s reads %u", head, kComposeName, reads.value);
    else
      note("pmp-xr-host: layers: %s, %s is absent", head, kComposeName);
  }

  void Presenter::probeCover()
  {
    struct VR_IVROverlay_FnTable* ovl = m_ovr.overlay;
    if (!ovl || !ovl->ComputeOverlayIntersection || m_panel == k_ulOverlayHandleInvalid)
      return;
    if (m_neighbours.empty())
      return;

    float corner[3];
    float unit[3];
    if (!panelPointAt(0.0f, 0.0f, corner) || !panelPointAt(1.0f, 0.0f, unit))
      return;

    float across = 1.0f;
    float up = 1.0f;
    if (m_panelMetres > 0.0f && gapBetween(corner, unit) < m_panelMetres * 0.5f &&
        m_panelMouseWide > 0.0f && m_panelMouseTall > 0.0f)
    {
      across = m_panelMouseWide;
      up = m_panelMouseTall;
    }

    struct HmdMatrix34_t pose;
    if (!panelPoseAt(across * 0.5f, up * 0.5f, pose))
      return;

    float right[3];
    float back[3];
    float middle[3];
    for (int axis = 0; axis < 3; ++axis)
    {
      right[axis] = pose.m[axis][0];
      back[axis] = pose.m[axis][2];
      middle[axis] = pose.m[axis][3];
    }

    float hit[3];
    float uv[2] = { -1.0f, -1.0f };
    float away = 0.0f;
    float sign = 0.0f;

    for (int facing = 0; facing < 2 && sign == 0.0f; ++facing)
    {
      const float try_ = facing == 0 ? 1.0f : -1.0f;
      float from[3];
      float along[3];
      for (int axis = 0; axis < 3; ++axis)
      {
        from[axis] = middle[axis] + back[axis] * 2.0f * try_;
        along[axis] = -back[axis] * try_;
      }
      if (castAt(from, along, hit, uv, away))
        sign = try_;
    }

    if (sign == 0.0f)
      return;

    const float span = m_panelMetres > 0.0f ? m_panelMetres : 2.0f;

    const int kSamples = 9;
    note("pmp-xr-host: probing straight through the panel at nine points across %.3f m", span);

    for (int sample = 0; sample < kSamples; ++sample)
    {
      const float at = (static_cast<float>(sample) / static_cast<float>(kSamples - 1) - 0.5f) *
                       span * 0.98f;

      float from[3];
      float along[3];
      for (int axis = 0; axis < 3; ++axis)
      {
        from[axis] = middle[axis] + back[axis] * 2.0f * sign + right[axis] * at;
        along[axis] = -back[axis] * sign;
      }

      float mine = 0.0f;
      const bool onPanel = castAt(from, along, hit, uv, mine);

      const char* nearest = nullptr;
      float best = onPanel ? mine : 1.0e9f;

      for (size_t other = 0; other < m_neighbours.size(); ++other)
      {
        float theirs = 0.0f;
        if (!castAtOverlay(m_neighbours[other].first, from, along, theirs))
          continue;
        if (theirs >= best - 0.002f)
          continue;
        best = theirs;
        nearest = m_neighbours[other].second.c_str();
      }

      if (!onPanel && !nearest)
        note("pmp-xr-host:   %+.3f m across: nothing is there", at);
      else if (!onPanel)
        note("pmp-xr-host:   %+.3f m across: the panel is not there, but %s is, at %.3f m", at,
             nearest, best);
      else if (!nearest)
        note("pmp-xr-host:   %+.3f m across: the panel at %.3f m, nothing in front of it", at,
             mine);
      else
        note("pmp-xr-host:   %+.3f m across: %s at %.3f m, standing %.3f m in front of the panel "
             "at %.3f m",
             at, nearest, best, mine - best, mine);
    }
  }

  bool Presenter::measureShape(float& wide, float& tall, float& bow, bool speak)
  {
    float origin[3];
    float unit[3];
    if (!panelPointAt(0.0f, 0.0f, origin) || !panelPointAt(1.0f, 0.0f, unit))
      return false;

    float across = 1.0f;
    float up = 1.0f;
    const char* how = "one coordinate step spans the panel";

    if (m_panelMetres > 0.0f && gapBetween(origin, unit) < m_panelMetres * 0.5f &&
        m_panelMouseWide > 0.0f && m_panelMouseTall > 0.0f)
    {
      across = m_panelMouseWide;
      up = m_panelMouseTall;
      how = "one coordinate step spans a texel";
    }

    float left[3];
    float middle[3];
    float right[3];
    float top[3];

    if (!panelPointAt(0.0f, 0.0f, left) || !panelPointAt(across * 0.5f, 0.0f, middle) ||
        !panelPointAt(across, 0.0f, right) || !panelPointAt(0.0f, up, top))
      return false;

    wide = gapBetween(left, right);
    tall = gapBetween(left, top);
    bow = bowOf(left, middle, right);
    if (!(wide > 0.0f) || !(tall > 0.0f))
      return false;

    if (speak)
      note("pmp-xr-host: the panel runs from %.4f %.4f %.4f through %.4f %.4f %.4f to %.4f "
           "%.4f %.4f and up to %.4f %.4f %.4f, %s so the span is %.1f by %.1f, bowing %.4f m "
           "over %.3f m, so it is %.3f by %.3f m, %.3f to 1",
           left[0], left[1], left[2], middle[0], middle[1], middle[2], right[0], right[1],
           right[2], top[0], top[1], top[2], how, across, up, bow, wide, wide, tall,
           wide / tall);
    return true;
  }

  void Presenter::fitPanel()
  {
    if (!(m_panelMouseWide > 0.0f) || !(m_panelMouseTall > 0.0f))
      return;

    const float want = texelWanted();
    if (m_texelWant != want)
    {
      char where[32];
      if (m_carrierLayer != 0)
        std::snprintf(where, sizeof(where), "layer %d", m_carrierLayer);
      else if (m_backed)
        std::snprintf(where, sizeof(where), "layer %d", m_backedLayer);
      else
        std::snprintf(where, sizeof(where), "the panel");

      note("pmp-xr-host: the window is %u by %u inside a %.0f by %.0f plane, packing %u, with "
           "the picture on %s, so the panel needs %.3f texels wide per tall",
           m_windowWide, m_windowTall, m_panelMouseWide, m_panelMouseTall, m_packing, where,
           want);
      m_texelWant = want;
      setTexelAspect(want);
    }

    if (m_shapeFailed)
      return;

    float bow = 0.0f;
    float wide = 0.0f;
    float tall = 0.0f;
    if (!measureBow(bow, wide, tall, false))
    {
      m_shapeFailed = true;
      m_arcKnown = false;
      note("pmp-xr-host: the panel's shape cannot be measured");
      return;
    }

    const bool bent = std::fabs(bow) > kBowCurved;
    const bool first = !(m_shapeHad > 0.0f);

    if (bent == m_bentSeen)
      ++m_bentAgree;
    else
    {
      m_bentSeen = bent;
      m_bentAgree = 1;
    }
    if (m_bentAgree < kBowAgree)
      return;

    const int64_t now = nowMs();

    if (!first && bent == m_shapeBent)
    {
      if (now - m_bowSaidAt >= kBowSayGapMs)
      {
        m_bowSaidAt = now;
        note("pmp-xr-host: the panel is still %s, bowing %.4f m over %.3f m, at %.3f texels "
             "wide per tall, from a %u by %u window in a %.0f by %.0f plane",
             bent ? "curved" : "flat", bow, wide, m_texelWant, m_windowWide, m_windowTall,
             m_panelMouseWide, m_panelMouseTall);
      }
      if (now - m_coverAt >= kBowSayGapMs)
      {
        m_coverAt = now;
        probeCover();
      }
      sayArc(false);
      return;
    }

    m_shapeHad = wide / tall;
    m_shapeBent = bent;
    m_shapeBow = bow;
    m_bowSaidAt = now;
    m_coverAt = now;

    measureBow(bow, wide, tall, true);
    sayArc(true);

    float flatWide = 0.0f;
    float flatTall = 0.0f;
    float flatBow = 0.0f;
    if (measureShape(flatWide, flatTall, flatBow, true))
      note("pmp-xr-host: the panel's own transform still says %.3f by %.3f m, %.3f to 1",
           flatWide, flatTall, flatWide / flatTall);

    note("pmp-xr-host: the panel is %s, bowing %.4f m over %.3f m, and stays at %.3f texels "
         "wide per tall",
         bent ? "curved" : "flat", bow, wide, m_texelWant);

    probeCover();
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
    m_shapeHad = 0.0f;
    m_shapeBent = false;
    m_shapeBow = 0.0f;
    m_shapeFailed = false;
    m_arcKnown = false;
    m_arcSaidRadius = -1.0f;
    m_arcSaidSweep = -1.0f;
    m_arcSaidWide = -1.0f;
    m_aimSaidBy = -1;
    m_aimPointed = k_unTrackedDeviceIndexInvalid;
    m_markAt = 0;

    if (m_texelWant > 1.0f)
      setTexelAspect(1.0f);
    m_texelWant = 0.0f;
    m_bentSeen = false;
    m_bentAgree = 0;
    m_bowSaidAt = 0;

    if (m_carrierLayer != 0)
    {
      const EVROverlayError error = ovl->SetOverlayFlag(
        m_layers[m_carrierLayer], VROverlayFlags_SideBySide_Parallel, false);
      note("pmp-xr-host: layer %d is flat again%s", m_carrierLayer,
           error == EVROverlayError_VROverlayError_None ? "" : " (the runtime complained)");
      m_carrierLayer = 0;
      return;
    }

    if (m_panelWasStereo)
      return;

    const EVROverlayError error =
      ovl->SetOverlayFlag(m_panel, VROverlayFlags_SideBySide_Parallel, false);
    note("pmp-xr-host: the panel is flat again%s",
         error == EVROverlayError_VROverlayError_None ? "" : " (the runtime complained)");

    measurePanel("after going flat");
  }

  void Presenter::hold()
  {
    const int64_t now = nowMs();
    if (now - m_checkAt < kCheckGapMs)
      return;
    m_checkAt = now;

    if (!m_settled && m_settledAt != 0 && now - m_settledAt > kSettleMs)
    {
      m_settled = true;
      m_relistAt = now + 30000;
      measurePanel("a while after going side by side");
    }

    if (m_settled && m_relists < 2 && m_relistAt != 0 && now >= m_relistAt)
    {
      ++m_relists;
      m_relistAt = now + 90000;
      m_surveyDone = false;
      m_surveyRunning = false;
      m_scanSeen = 0;
      m_scanProbes = 0;
      note("pmp-xr-host: listing the overlays again while the panel is live, %d of 2",
           m_relists);
    }

    measurePanel("while the panel is live", true);

    if (now - m_dotAt >= kDotGapMs)
    {
      m_dotAt = now;
      probeCursor();
    }

    struct VR_IVROverlay_FnTable* ovl = m_ovr.overlay;

    bool enabled = false;
    const EVROverlayError read =
      ovl->GetOverlayFlag(m_panel, VROverlayFlags_SideBySide_Parallel, &enabled);

    if (read != EVROverlayError_VROverlayError_None)
    {
      note("pmp-xr-host: the Plex panel went away (%s), looking for it again", errorName(read));
      m_stereo = false;
      m_panel = k_ulOverlayHandleInvalid;
      m_carrierLayer = 0;
      m_backed = false;
      m_stage = Looking;
      m_lookTries = 0;
      send(XrProtocol::Flat, "the Plex panel was replaced, waiting for the new one");
      return;
    }

    if (m_carrierLayer != 0)
    {
      bool carried = false;
      const EVROverlayError layerRead = ovl->GetOverlayFlag(
        m_layers[m_carrierLayer], VROverlayFlags_SideBySide_Parallel, &carried);

      if (layerRead == EVROverlayError_VROverlayError_None)
      {
        if (!carried)
        {
          const EVROverlayError again = ovl->SetOverlayFlag(
            m_layers[m_carrierLayer], VROverlayFlags_SideBySide_Parallel, true);
          note("pmp-xr-host: something cleared the side by side flag on layer %d, and it was "
               "set again, %s",
               m_carrierLayer, errorName(again));
        }
        fitPanel();
        return;
      }

      note("pmp-xr-host: layer %d went away (%s), so the panel takes the side by side flag "
           "again",
           m_carrierLayer, errorName(layerRead));
      m_layers[m_carrierLayer] = k_ulOverlayHandleInvalid;
      m_carrierLayer = 0;
    }
    else if (enabled)
    {
      fitPanel();
      return;
    }
    else
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
    message.mouseWide = static_cast<uint32_t>(m_panelMouseWide + 0.5f);
    message.mouseTall = static_cast<uint32_t>(m_panelMouseTall + 0.5f);
    message.panelMillimetres = static_cast<uint32_t>(m_panelMetres * 1000.0f + 0.5f);
    message.texelAspectThousandths =
      static_cast<uint32_t>(m_panelTexelAspect * 1000.0f + 0.5f);

    const bool pointing = type == XrProtocol::Stereo || type == XrProtocol::Pointer;
    const float pointer = pointing ? pointerWide() : 1.0f;
    message.pointerWideThousandths = static_cast<uint32_t>(pointer * 1000.0f + 0.5f);

    if (pointing)
    {
      takeAim();
      curveNow(message.curveRadiusMillimetres, message.curveSweepThousandths,
               message.curveTallMillimetres, message.aimedBy, message.aimMillimetres);
      m_curveTold[0] = message.curveRadiusMillimetres;
      m_curveTold[1] = message.curveSweepThousandths;
      m_curveTold[2] = message.curveTallMillimetres;
      m_curveTold[3] = message.aimedBy;
      for (int axis = 0; axis < 3; ++axis)
        m_aimTold[axis] = message.aimMillimetres[axis];
    }
    else
    {
      for (int which = 0; which < 4; ++which)
        m_curveTold[which] = 0;
      for (int axis = 0; axis < 3; ++axis)
        m_aimTold[axis] = 0;
    }

    if (!pointing)
      m_pointerWideTold = 0.0f;
    else if (pointer != m_pointerWideTold)
    {
      m_pointerWideTold = pointer;
      char where[32];
      if (m_carrierLayer != 0)
        std::snprintf(where, sizeof(where), "layer %d", m_carrierLayer);
      else
        std::snprintf(where, sizeof(where), "the panel");
      note("pmp-xr-host: the laser lands on a surface %.3f times as wide as the picture, with "
           "side by side on %s and the window on %s",
           pointer, where, m_backed ? "a layer" : "the panel");
    }
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

      if (message.packing != m_packing)
      {
        m_packing = message.packing;
        note("pmp-xr-host: the app packs the picture as %u", m_packing);
      }

      if (message.windowWide >= 2 && message.windowTall >= 2 &&
          (message.windowWide != m_windowWide || message.windowTall != m_windowTall))
      {
        m_windowWide = message.windowWide;
        m_windowTall = message.windowTall;
        note("pmp-xr-host: the app draws a %u by %u window into a %.0f by %.0f plane",
             m_windowWide, m_windowTall, m_panelMouseWide, m_panelMouseTall);
        if (m_stage == Holding)
          watchLayers(true);
        if (m_stereo)
          fitPanel();
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

        case XrProtocol::Mark:
          m_markAcross = float(message.markAcrossMillionths) / 1000000.0f;
          m_markDown = float(message.markDownMillionths) / 1000000.0f;
          m_markAt = nowMs();
          if (message.markSay != 0 && m_stage == Holding)
          {
            takeAim();
            sayDevices();
          }
          break;

        case XrProtocol::Stop:
          note("pmp-xr-host: the app asked to stop");
          dropStereo();
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

    const RootProperty::Reading left = RootProperty::read(kComposeOurs);
    if (left.reached && left.present)
    {
      RootProperty::remove(kComposeName);
      RootProperty::remove(kComposeOurs);
      note("pmp-xr-host: an earlier run left %s set, and it was taken away", kComposeName);
    }
    else if (!left.reached)
      note("pmp-xr-host: the X root window cannot be read, %s", left.trouble.c_str());

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

      if ((m_stage == Looking && m_lookTries >= 1) || (m_stage == Holding && !m_surveyDone))
        surveySlice();

      if (m_stage == Holding)
      {
        watchLayers();
        tellPointer();
      }

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
    dropComposite();
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
