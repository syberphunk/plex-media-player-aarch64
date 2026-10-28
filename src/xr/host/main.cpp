#include <openxr/openxr.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Present.h"
#include "../XrProtocol.h"

#ifdef PMP_XR_HOST_VULKAN
#include <vulkan/vulkan.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr_platform.h>
#endif

namespace
{
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

  void fail(const std::string& stage, XrInstance instance, XrResult result)
  {
    char buffer[XR_MAX_RESULT_STRING_SIZE] = {0};
    if (instance != XR_NULL_HANDLE && XR_SUCCEEDED(xrResultToString(instance, result, buffer)))
      g_error = stage + ": " + buffer;
    else
      g_error = stage + ": " + std::to_string(static_cast<int>(result));
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

  const char* blendModeName(XrEnvironmentBlendMode mode)
  {
    switch (mode)
    {
      case XR_ENVIRONMENT_BLEND_MODE_OPAQUE: return "opaque";
      case XR_ENVIRONMENT_BLEND_MODE_ADDITIVE: return "additive";
      case XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND: return "alpha-blend";
      default: return "unknown";
    }
  }

  std::string versionString(XrVersion version)
  {
    return std::to_string(XR_VERSION_MAJOR(version)) + "." +
           std::to_string(XR_VERSION_MINOR(version)) + "." +
           std::to_string(XR_VERSION_PATCH(version));
  }

#ifdef PMP_XR_HOST_VULKAN
  bool createSession(XrInstance instance, XrSystemId system, Result& out)
  {
    PFN_xrGetVulkanGraphicsRequirements2KHR getRequirements = nullptr;
    PFN_xrCreateVulkanInstanceKHR createVulkanInstance = nullptr;
    PFN_xrGetVulkanGraphicsDevice2KHR getGraphicsDevice = nullptr;
    PFN_xrCreateVulkanDeviceKHR createVulkanDevice = nullptr;

    if (XR_FAILED(xrGetInstanceProcAddr(instance, "xrGetVulkanGraphicsRequirements2KHR",
                                        reinterpret_cast<PFN_xrVoidFunction*>(&getRequirements))) ||
        XR_FAILED(xrGetInstanceProcAddr(instance, "xrCreateVulkanInstanceKHR",
                                        reinterpret_cast<PFN_xrVoidFunction*>(&createVulkanInstance))) ||
        XR_FAILED(xrGetInstanceProcAddr(instance, "xrGetVulkanGraphicsDevice2KHR",
                                        reinterpret_cast<PFN_xrVoidFunction*>(&getGraphicsDevice))) ||
        XR_FAILED(xrGetInstanceProcAddr(instance, "xrCreateVulkanDeviceKHR",
                                        reinterpret_cast<PFN_xrVoidFunction*>(&createVulkanDevice))))
    {
      g_error = "vulkan-enable2 entry points missing";
      return false;
    }

    XrGraphicsRequirementsVulkan2KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
    XrResult result = getRequirements(instance, system, &requirements);
    if (XR_FAILED(result))
    {
      fail("graphics requirements", instance, result);
      return false;
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "pmp-xr-host";
    appInfo.applicationVersion = 1;
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &appInfo;

    XrVulkanInstanceCreateInfoKHR xrInstanceInfo{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
    xrInstanceInfo.systemId = system;
    xrInstanceInfo.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
    xrInstanceInfo.vulkanCreateInfo = &instanceInfo;

    VkInstance vkInstance = VK_NULL_HANDLE;
    VkResult vkResult = VK_SUCCESS;
    result = createVulkanInstance(instance, &xrInstanceInfo, &vkInstance, &vkResult);
    if (XR_FAILED(result) || vkResult != VK_SUCCESS)
    {
      g_error = "vulkan instance: xr=" + std::to_string(static_cast<int>(result)) +
                " vk=" + std::to_string(static_cast<int>(vkResult));
      return false;
    }

    XrVulkanGraphicsDeviceGetInfoKHR deviceGetInfo{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
    deviceGetInfo.systemId = system;
    deviceGetInfo.vulkanInstance = vkInstance;

    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    result = getGraphicsDevice(instance, &deviceGetInfo, &physicalDevice);
    if (XR_FAILED(result))
    {
      fail("graphics device", instance, result);
      vkDestroyInstance(vkInstance, nullptr);
      return false;
    }

    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());

    uint32_t graphicsFamily = 0;
    bool haveFamily = false;
    for (uint32_t i = 0; i < familyCount; ++i)
    {
      if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
      {
        graphicsFamily = i;
        haveFamily = true;
        break;
      }
    }
    if (!haveFamily)
    {
      g_error = "no graphics queue family";
      vkDestroyInstance(vkInstance, nullptr);
      return false;
    }

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = graphicsFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;

    XrVulkanDeviceCreateInfoKHR xrDeviceInfo{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
    xrDeviceInfo.systemId = system;
    xrDeviceInfo.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
    xrDeviceInfo.vulkanPhysicalDevice = physicalDevice;
    xrDeviceInfo.vulkanCreateInfo = &deviceInfo;

    VkDevice device = VK_NULL_HANDLE;
    result = createVulkanDevice(instance, &xrDeviceInfo, &device, &vkResult);
    if (XR_FAILED(result) || vkResult != VK_SUCCESS)
    {
      g_error = "vulkan device: xr=" + std::to_string(static_cast<int>(result)) +
                " vk=" + std::to_string(static_cast<int>(vkResult));
      vkDestroyInstance(vkInstance, nullptr);
      return false;
    }

    XrGraphicsBindingVulkan2KHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
    binding.instance = vkInstance;
    binding.physicalDevice = physicalDevice;
    binding.device = device;
    binding.queueFamilyIndex = graphicsFamily;
    binding.queueIndex = 0;

    XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
    sessionInfo.next = &binding;
    sessionInfo.systemId = system;

    XrSession session = XR_NULL_HANDLE;
    result = xrCreateSession(instance, &sessionInfo, &session);
    if (XR_FAILED(result))
    {
      fail("session", instance, result);
      vkDestroyDevice(device, nullptr);
      vkDestroyInstance(vkInstance, nullptr);
      return false;
    }
    out.session = true;

    uint32_t formatCount = 0;
    xrEnumerateSwapchainFormats(session, 0, &formatCount, nullptr);
    std::vector<int64_t> formats(formatCount);
    if (formatCount > 0)
      xrEnumerateSwapchainFormats(session, formatCount, &formatCount, formats.data());

    int64_t chosen = formats.empty() ? 0 : formats[0];
    const int64_t preferred[] = {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB,
                                 VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM};
    for (int64_t want : preferred)
    {
      bool found = false;
      for (int64_t have : formats)
        if (have == want)
          found = true;
      if (found)
      {
        chosen = want;
        break;
      }
    }

    std::vector<XrSwapchain> swapchains;
    for (int view = 0; view < out.viewCount; ++view)
    {
      XrSwapchainCreateInfo swapchainInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
      swapchainInfo.usageFlags =
        XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
      swapchainInfo.format = chosen;
      swapchainInfo.sampleCount = 1;
      swapchainInfo.width = static_cast<uint32_t>(out.width);
      swapchainInfo.height = static_cast<uint32_t>(out.height);
      swapchainInfo.faceCount = 1;
      swapchainInfo.arraySize = 1;
      swapchainInfo.mipCount = 1;

      XrSwapchain swapchain = XR_NULL_HANDLE;
      result = xrCreateSwapchain(session, &swapchainInfo, &swapchain);
      if (XR_FAILED(result))
      {
        fail("swapchain " + std::to_string(view), instance, result);
        break;
      }
      swapchains.push_back(swapchain);
      out.swapchains++;
    }

    for (XrSwapchain swapchain : swapchains)
      xrDestroySwapchain(swapchain);
    xrDestroySession(session);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(vkInstance, nullptr);
    return out.swapchains == out.viewCount;
  }
#endif

  bool run(bool wantSession, Result& out)
  {
    uint32_t extensionCount = 0;
    XrResult result = xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);
    if (XR_FAILED(result))
    {
      fail("extension count", XR_NULL_HANDLE, result);
      return false;
    }

    std::vector<XrExtensionProperties> properties(
      extensionCount, XrExtensionProperties{XR_TYPE_EXTENSION_PROPERTIES});
    if (extensionCount > 0 &&
        XR_FAILED(xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount,
                                                         properties.data())))
    {
      g_error = "extension list unreadable";
      return false;
    }
    for (const XrExtensionProperties& property : properties)
      out.extensions.push_back(property.extensionName);

    std::vector<const char*> enabled;
#ifdef PMP_XR_HOST_VULKAN
    if (wantSession)
    {
      bool haveVulkan2 = false;
      for (const std::string& name : out.extensions)
        if (name == XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME)
          haveVulkan2 = true;
      if (!haveVulkan2)
      {
        g_error = "runtime has no XR_KHR_vulkan_enable2";
        return false;
      }
      enabled.push_back(XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME);
    }
#endif

    XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
    createInfo.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    std::strncpy(createInfo.applicationInfo.applicationName, "Plex Media Player",
                 XR_MAX_APPLICATION_NAME_SIZE - 1);
    std::strncpy(createInfo.applicationInfo.engineName, "pmp-xr-host",
                 XR_MAX_ENGINE_NAME_SIZE - 1);
    createInfo.applicationInfo.applicationVersion = 1;
    createInfo.applicationInfo.engineVersion = 1;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(enabled.size());
    createInfo.enabledExtensionNames = enabled.empty() ? nullptr : enabled.data();

    XrInstance instance = XR_NULL_HANDLE;
    result = xrCreateInstance(&createInfo, &instance);
    if (XR_FAILED(result))
    {
      fail("instance", XR_NULL_HANDLE, result);
      return false;
    }
    out.instance = true;

    XrInstanceProperties instanceProperties{XR_TYPE_INSTANCE_PROPERTIES};
    if (XR_SUCCEEDED(xrGetInstanceProperties(instance, &instanceProperties)))
    {
      out.runtimeName = instanceProperties.runtimeName;
      out.runtimeVersion = versionString(instanceProperties.runtimeVersion);
    }

    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    result = xrGetSystem(instance, &systemInfo, &system);
    if (XR_FAILED(result))
    {
      fail("system", instance, result);
      xrDestroyInstance(instance);
      return false;
    }
    out.system = true;

    XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
    if (XR_SUCCEEDED(xrGetSystemProperties(instance, system, &systemProperties)))
      out.systemName = systemProperties.systemName;

    uint32_t viewCount = 0;
    if (XR_SUCCEEDED(xrEnumerateViewConfigurationViews(
          instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr)) &&
        viewCount > 0)
    {
      std::vector<XrViewConfigurationView> views(
        viewCount, XrViewConfigurationView{XR_TYPE_VIEW_CONFIGURATION_VIEW});
      if (XR_SUCCEEDED(xrEnumerateViewConfigurationViews(
            instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount, &viewCount,
            views.data())))
      {
        out.viewCount = static_cast<int>(viewCount);
        out.width = static_cast<int>(views[0].recommendedImageRectWidth);
        out.height = static_cast<int>(views[0].recommendedImageRectHeight);
        out.samples = static_cast<int>(views[0].recommendedSwapchainSampleCount);
      }
    }

    uint32_t modeCount = 0;
    if (XR_SUCCEEDED(xrEnumerateEnvironmentBlendModes(
          instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &modeCount, nullptr)) &&
        modeCount > 0)
    {
      std::vector<XrEnvironmentBlendMode> modes(modeCount);
      if (XR_SUCCEEDED(xrEnumerateEnvironmentBlendModes(
            instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, modeCount, &modeCount,
            modes.data())))
      {
        for (XrEnvironmentBlendMode mode : modes)
          out.blendModes.push_back(blendModeName(mode));
      }
    }

    bool ok = true;
#ifdef PMP_XR_HOST_VULKAN
    if (wantSession && out.viewCount > 0)
      ok = createSession(instance, system, out);
#else
    if (wantSession)
    {
      g_error = "built without Vulkan";
      ok = false;
    }
#endif

    xrDestroyInstance(instance);
    return ok;
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
