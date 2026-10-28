#ifndef PMPXROPENVR_H
#define PMPXROPENVR_H

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "openvr_capi.h"
#pragma GCC diagnostic pop

#include <stdint.h>

#include <string>
#include <vector>

namespace OpenVr
{
  class Api
  {
  public:
    ~Api();

    bool load();
    bool start(EVRApplicationType type);
    void stop();

    bool loaded() const { return m_library != nullptr; }
    bool started() const { return m_started; }
    bool runtimeInstalled() const;
    bool hmdPresent() const;

    const std::string& error() const { return m_error; }
    const std::string& libraryPath() const { return m_libraryPath; }
    const std::vector<std::string>& interfaces() const { return m_interfaces; }

    std::string runtimePath() const;
    std::string deviceString(ETrackedDeviceProperty property) const;
    bool recommendedSize(uint32_t& width, uint32_t& height) const;

    struct VR_IVRSystem_FnTable* system = nullptr;
    struct VR_IVROverlay_FnTable* overlay = nullptr;
    struct VR_IVRCompositor_FnTable* compositor = nullptr;

  private:
    void* lookup(const char* name) const;
    void* table(const char* version);

    void* m_library = nullptr;
    bool m_started = false;
    std::string m_error;
    std::string m_libraryPath;
    std::vector<std::string> m_interfaces;

    uint32_t (*m_initInternal2)(EVRInitError*, EVRApplicationType, const char*) = nullptr;
    intptr_t (*m_initInternal)(EVRInitError*, EVRApplicationType) = nullptr;
    void (*m_shutdownInternal)() = nullptr;
    intptr_t (*m_getInterface)(const char*, EVRInitError*) = nullptr;
    bool (*m_isHmdPresent)() = nullptr;
    bool (*m_isRuntimeInstalled)() = nullptr;
    bool (*m_getRuntimePath)(char*, uint32_t, uint32_t*) = nullptr;
    const char* (*m_errorText)(EVRInitError) = nullptr;
  };
}

#endif // PMPXROPENVR_H
