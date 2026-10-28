#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "OpenVr.h"
#include "Present.h"
#include "../XrProtocol.h"

namespace
{
  char kProbeOverlayKey[] = "io.github.syberphunk.PlexMediaPlayer.probe";
  char kProbeOverlayName[] = "Plex Media Player probe";

  std::string g_error;

  std::string escape(const char* text)
  {
    std::string out;
    for (const char* p = text; p && *p; ++p)
    {
      switch (*p)
      {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': break;
        case '\t': out += "\\t"; break;
        default:
          if (static_cast<unsigned char>(*p) < 0x20)
            out += ' ';
          else
            out += *p;
      }
    }
    return out;
  }

  struct Result
  {
    bool instance = false;
    bool system = false;
    bool session = false;
    std::string runtimeName;
    std::string runtimeVersion;
    std::string systemName;
    int viewCount = 0;
    int width = 0;
    int height = 0;
    int samples = 0;
    int swapchains = 0;
    std::vector<std::string> extensions;
    std::vector<std::string> blendModes;
  };

  bool openOverlay(OpenVr::Api& api, Result& out)
  {
    if (!api.overlay)
    {
      g_error = std::string("the runtime does not serve ") + IVROverlay_Version;
      return false;
    }

    VROverlayHandle_t handle = k_ulOverlayHandleInvalid;
    EVROverlayError error = api.overlay->CreateOverlay(kProbeOverlayKey, kProbeOverlayName,
                                                       &handle);
    if (error == EVROverlayError_VROverlayError_KeyInUse)
      error = api.overlay->FindOverlay(kProbeOverlayKey, &handle);

    if (error != EVROverlayError_VROverlayError_None || handle == k_ulOverlayHandleInvalid)
    {
      const char* text = api.overlay->GetOverlayErrorNameFromEnum
                           ? api.overlay->GetOverlayErrorNameFromEnum(error)
                           : nullptr;
      g_error = std::string("overlay: ") +
                (text ? text : std::to_string(static_cast<int>(error)));
      return false;
    }

    api.overlay->SetOverlayFlag(handle, VROverlayFlags_SideBySide_Parallel, true);
    api.overlay->SetOverlayFlag(handle, VROverlayFlags_IgnoreTextureAlpha, true);
    api.overlay->SetOverlayFlag(handle, VROverlayFlags_SortWithNonSceneOverlays, true);
    api.overlay->SetOverlayWidthInMeters(handle, 2.4f);

    error = api.overlay->ShowOverlay(handle);
    if (error != EVROverlayError_VROverlayError_None)
    {
      const char* text = api.overlay->GetOverlayErrorNameFromEnum
                           ? api.overlay->GetOverlayErrorNameFromEnum(error)
                           : nullptr;
      g_error = std::string("show overlay: ") +
                (text ? text : std::to_string(static_cast<int>(error)));
      api.overlay->DestroyOverlay(handle);
      return false;
    }

    api.overlay->HideOverlay(handle);
    api.overlay->DestroyOverlay(handle);

    out.session = true;
    out.swapchains = 1;
    return true;
  }

  bool run(bool wantSession, Result& out)
  {
    OpenVr::Api api;

    if (!api.load())
    {
      g_error = api.error();
      return false;
    }

    if (!api.runtimeInstalled())
    {
      g_error = "no SteamVR runtime is installed";
      return false;
    }

    if (!api.start(EVRApplicationType_VRApplication_Overlay))
    {
      g_error = api.error();
      return false;
    }

    out.instance = true;

    out.runtimeName = "SteamVR";
    const std::string path = api.runtimePath();
    std::fprintf(stderr, "pmp-xr-host: %s from %s\n",
                 path.empty() ? "(unknown runtime path)" : path.c_str(),
                 api.libraryPath().c_str());
    out.runtimeVersion = api.deviceString(ETrackedDeviceProperty_Prop_DriverVersion_String);

    for (const std::string& version : api.interfaces())
      out.extensions.push_back(version);

    if (!api.hmdPresent())
    {
      g_error = "no headset is connected";
      return false;
    }

    out.system = true;
    out.viewCount = 2;
    out.samples = 1;
    out.systemName = api.deviceString(ETrackedDeviceProperty_Prop_ModelNumber_String);

    uint32_t width = 0;
    uint32_t height = 0;
    if (api.recommendedSize(width, height))
    {
      out.width = static_cast<int>(width);
      out.height = static_cast<int>(height);
    }

    if (!wantSession)
      return true;

    return openOverlay(api, out);
  }

  void emit(const Result& out, bool ok)
  {
    std::printf("{\n");
    std::printf("  \"ok\": %s,\n", ok ? "true" : "false");
    std::printf("  \"instance\": %s,\n", out.instance ? "true" : "false");
    std::printf("  \"system\": %s,\n", out.system ? "true" : "false");
    std::printf("  \"session\": %s,\n", out.session ? "true" : "false");
    std::printf("  \"swapchains\": %d,\n", out.swapchains);
    std::printf("  \"runtimeName\": \"%s\",\n", escape(out.runtimeName.c_str()).c_str());
    std::printf("  \"runtimeVersion\": \"%s\",\n", escape(out.runtimeVersion.c_str()).c_str());
    std::printf("  \"systemName\": \"%s\",\n", escape(out.systemName.c_str()).c_str());
    std::printf("  \"viewCount\": %d,\n", out.viewCount);
    std::printf("  \"width\": %d,\n", out.width);
    std::printf("  \"height\": %d,\n", out.height);
    std::printf("  \"sampleCount\": %d,\n", out.samples);

    std::printf("  \"blendModes\": [");
    for (size_t i = 0; i < out.blendModes.size(); ++i)
      std::printf("%s\"%s\"", i ? ", " : "", escape(out.blendModes[i].c_str()).c_str());
    std::printf("],\n");

    std::printf("  \"extensions\": [");
    for (size_t i = 0; i < out.extensions.size(); ++i)
      std::printf("%s\"%s\"", i ? ", " : "", escape(out.extensions[i].c_str()).c_str());
    std::printf("],\n");

    std::printf("  \"error\": \"%s\"\n", escape(g_error.c_str()).c_str());
    std::printf("}\n");
  }
}

int main(int argc, char** argv)
{
  const std::string command = argc > 1 ? argv[1] : "probe";
  if (command != "probe" && command != "session" && command != "present")
  {
    std::fprintf(stderr, "usage: %s [probe|session|present]\n", argv[0]);
    return 2;
  }

  if (command == "present")
    return XrPresent::run(XrProtocol::kSocketFd);

  Result out;
  const bool ok = run(command == "session", out);
  emit(out, ok);
  return ok ? 0 : 1;
}
