#include "OpenVr.h"

#include <dlfcn.h>

#include <cstring>

namespace
{
  const char* const kCandidates[] = {"libopenvr_api.so",
                                     "/usr/lib/libopenvr_api.so",
                                     "/opt/steamvr/bin/linuxarm64/libopenvr_api.so",
                                     "/opt/steamvr/bin/linux64/libopenvr_api.so"};
}

namespace OpenVr
{
  Api::~Api()
  {
    stop();
    if (m_library)
      dlclose(m_library);
    m_library = nullptr;
  }

  void* Api::lookup(const char* name) const
  {
    return m_library ? dlsym(m_library, name) : nullptr;
  }

  bool Api::load()
  {
    if (m_library)
      return true;

    for (const char* candidate : kCandidates)
    {
      m_library = dlopen(candidate, RTLD_NOW | RTLD_LOCAL);
      if (m_library)
      {
        m_libraryPath = candidate;
        break;
      }
    }

    if (!m_library)
    {
      m_error = "libopenvr_api.so was not found";
      return false;
    }

    m_initInternal2 = reinterpret_cast<uint32_t (*)(EVRInitError*, EVRApplicationType, const char*)>(
      lookup("VR_InitInternal2"));
    m_initInternal =
      reinterpret_cast<intptr_t (*)(EVRInitError*, EVRApplicationType)>(lookup("VR_InitInternal"));
    m_shutdownInternal = reinterpret_cast<void (*)()>(lookup("VR_ShutdownInternal"));
    m_getInterface =
      reinterpret_cast<intptr_t (*)(const char*, EVRInitError*)>(lookup("VR_GetGenericInterface"));
    m_isHmdPresent = reinterpret_cast<bool (*)()>(lookup("VR_IsHmdPresent"));
    m_isRuntimeInstalled = reinterpret_cast<bool (*)()>(lookup("VR_IsRuntimeInstalled"));
    m_getRuntimePath =
      reinterpret_cast<bool (*)(char*, uint32_t, uint32_t*)>(lookup("VR_GetRuntimePath"));
    m_errorText = reinterpret_cast<const char* (*)(EVRInitError)>(
      lookup("VR_GetVRInitErrorAsEnglishDescription"));

    if ((!m_initInternal2 && !m_initInternal) || !m_shutdownInternal || !m_getInterface)
    {
      m_error = "libopenvr_api.so is missing its entry points";
      return false;
    }

    return true;
  }

  bool Api::runtimeInstalled() const
  {
    return m_isRuntimeInstalled ? m_isRuntimeInstalled() : m_library != nullptr;
  }

  bool Api::hmdPresent() const
  {
    return m_isHmdPresent ? m_isHmdPresent() : false;
  }

  std::string Api::runtimePath() const
  {
    if (!m_getRuntimePath)
      return std::string();
    char buffer[1024] = {0};
    uint32_t required = 0;
    if (!m_getRuntimePath(buffer, sizeof(buffer), &required))
      return std::string();
    return buffer;
  }

  void* Api::table(const char* version)
  {
    if (!m_getInterface)
      return nullptr;

    const std::string name = std::string("FnTable:") + version;
    EVRInitError error = EVRInitError_VRInitError_None;
    void* found = reinterpret_cast<void*>(m_getInterface(name.c_str(), &error));
    if (!found || error != EVRInitError_VRInitError_None)
      return nullptr;

    m_interfaces.push_back(version);
    return found;
  }

  bool Api::start(EVRApplicationType type)
  {
    if (!load())
      return false;
    if (m_started)
      return true;

    EVRInitError error = EVRInitError_VRInitError_None;
    if (m_initInternal2)
      m_initInternal2(&error, type, nullptr);
    else
      m_initInternal(&error, type);

    if (error != EVRInitError_VRInitError_None)
    {
      const char* text = m_errorText ? m_errorText(error) : nullptr;
      m_error = std::string("the runtime refused to start: ") +
                (text ? text : std::to_string(static_cast<int>(error)));
      return false;
    }

    m_started = true;
    m_interfaces.clear();

    system = reinterpret_cast<struct VR_IVRSystem_FnTable*>(table(IVRSystem_Version));
    overlay = reinterpret_cast<struct VR_IVROverlay_FnTable*>(table(IVROverlay_Version));
    compositor =
      reinterpret_cast<struct VR_IVRCompositor_FnTable*>(table(IVRCompositor_Version));

    if (!system)
    {
      m_error = std::string("the runtime does not serve ") + IVRSystem_Version;
      stop();
      return false;
    }

    return true;
  }

  void Api::stop()
  {
    if (!m_started)
      return;
    system = nullptr;
    overlay = nullptr;
    compositor = nullptr;
    m_started = false;
    if (m_shutdownInternal)
      m_shutdownInternal();
  }

  std::string Api::deviceString(ETrackedDeviceProperty property) const
  {
    if (!system || !system->GetStringTrackedDeviceProperty)
      return std::string();

    char buffer[512] = {0};
    ETrackedPropertyError error = ETrackedPropertyError_TrackedProp_Success;
    const uint32_t length = system->GetStringTrackedDeviceProperty(
      k_unTrackedDeviceIndex_Hmd, property, buffer, sizeof(buffer), &error);
    if (length == 0 || error != ETrackedPropertyError_TrackedProp_Success)
      return std::string();
    return buffer;
  }

  bool Api::recommendedSize(uint32_t& width, uint32_t& height) const
  {
    if (!system || !system->GetRecommendedRenderTargetSize)
      return false;
    width = 0;
    height = 0;
    system->GetRecommendedRenderTargetSize(&width, &height);
    return width > 0 && height > 0;
  }
}
