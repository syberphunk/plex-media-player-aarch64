#include "Present.h"

#ifdef PMP_XR_HOST_VULKAN

#include <vulkan/vulkan.h>

#include "OpenVr.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../XrProtocol.h"

extern "C" char** environ;

namespace
{
  const float kScreenDistance = 2.0f;
  const float kScreenWidth = 2.4f;
  const float kDefaultAspect = 16.0f / 9.0f;
  const float kDefaultEyeHeight = 1.6f;
  const int kPlaceAttempts = 90;
  const uint32_t kSortOrder = 100;
  const float kDockNudge = 0.01f;
  const float kDockMinWidth = 0.3f;
  const float kDockMaxWidth = 60.0f;
  const int kSurveyGapMs = 2000;
  const int kSurveyPatientGapMs = 10000;
  const int kSurveyLoudRounds = 2;
  const int kScanBudget = 2048;
  const int kScanSliceMs = 2;
  const int kDockLossLimit = 30;
  const uint32_t kScanIndexes = 128;
  const uint32_t kScanAhead = 8;
  const uint64_t kModifierInvalid = 0x00ffffffffffffffULL;
  const uint32_t kFrameSyncTimeoutMs = 100;

  char kOverlayKey[] = "io.github.syberphunk.PlexMediaPlayer.screen";
  char kOverlayName[] = "Plex Media Player";

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

  void sleepMs(int ms)
  {
    struct timespec request = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&request, nullptr);
  }

  int64_t nowMs()
  {
    struct timespec now = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<int64_t>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
  }

  struct Neighbour
  {
    VROverlayHandle_t handle = k_ulOverlayHandleInvalid;
    char key[128] = {0};
    char name[128] = {0};
    float width = 0.0f;
    float curvature = 0.0f;
    float pitch = 0.0f;
    uint32_t flags = 0;
    uint32_t sortOrder = 0;
    uint32_t pid = 0;
    uint32_t textureWidth = 0;
    uint32_t textureHeight = 0;
    int transformType = -1;
    bool visible = false;
    bool placed = false;
    ETrackingUniverseOrigin origin = ETrackingUniverseOrigin_TrackingUniverseStanding;
    HmdMatrix34_t transform{};
  };

  bool checkVk(VkResult result, const char* stage)
  {
    if (result == VK_SUCCESS)
      return true;
    note("pmp-xr-host: %s failed (%d)", stage, static_cast<int>(result));
    return false;
  }

  struct Source
  {
    int fd = -1;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    bool ready = false;
  };

  class Presenter
  {
  public:
    explicit Presenter(int socketFd) : m_socket(socketFd) {}
    ~Presenter() { teardown(); }

    bool setup();
    int run();

  private:
    bool startRuntime();
    bool createDevice();
    bool createOverlay();
    bool importBuffers(const XrProtocol::Message& message, const int* fds, int count);
    bool checkRowPitch(VkImage image, uint32_t stride) const;
    bool checkSize(int fd, VkDeviceSize needed, const XrProtocol::Message& message) const;
    bool ensureTarget();
    void teardown();

    bool readSocket();
    void send(XrProtocol::Type type, uint32_t index);

    void pollEvents(bool& exit);
    void frame();
    bool blitEyes();
    void placeScreen();
    bool headPose(float position[3], float forward[3]) const;
    void describeEnvironment();
    bool readNeighbour(VROverlayHandle_t handle, Neighbour& out) const;
    bool dockable(const Neighbour& candidate) const;
    int dockScore(const Neighbour& candidate) const;
    void survey();
    void finishSurvey();
    void adoptDock(const Neighbour& chosen);
    void applyDock(const Neighbour& window, bool force);
    void trackDock();
    void setVisible(bool visible);
    void updateVisibility();

    uint32_t memoryType(uint32_t bits, VkMemoryPropertyFlags flags) const;
    std::vector<const char*> wantedInstanceExtensions();
    std::vector<const char*> wantedDeviceExtensions();

    int m_socket = -1;

    OpenVr::Api m_ovr;
    VROverlayHandle_t m_overlay = k_ulOverlayHandleInvalid;
    bool m_screenPlaced = false;
    int m_placeAttempts = 0;
    bool m_appVisible = true;
    bool m_dockVisible = true;
    bool m_shown = true;

    bool m_environmentDescribed = false;
    bool m_surveyRunning = false;
    bool m_surveyDone = false;
    int m_surveyRounds = 0;
    int64_t m_surveyAt = 0;
    uint32_t m_scanGeneration = 0;
    uint32_t m_scanIndex = 0;
    uint32_t m_topGeneration = 0;
    int m_scanSeen = 0;
    int m_scanProbes = 0;
    Neighbour m_best;
    bool m_haveBest = false;

    VROverlayHandle_t m_dock = k_ulOverlayHandleInvalid;
    std::string m_dockKey;
    bool m_docked = false;
    int m_dockLosses = 0;
    float m_dockedWidth = 0.0f;
    float m_dockNudge = kDockNudge;
    float m_dockedCurvature = -1.0f;
    float m_dockedPitch = 0.0f;
    uint32_t m_dockedSortOrder = kSortOrder;
    HmdMatrix34_t m_dockedTransform{};

    VkInstance m_vkInstance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    uint32_t m_queueFamily = 0;
    VkCommandPool m_pool = VK_NULL_HANDLE;
    VkCommandBuffer m_command = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties m_memory{};
    PFN_vkGetMemoryFdPropertiesKHR m_getMemoryFdProperties = nullptr;
    uint32_t m_maxDimension = 4096;

    std::string m_runtimeExtensions;
    std::string m_runtimeDeviceExtensions;

    VkFormat m_sourceFormat = VK_FORMAT_R8G8B8A8_UNORM;
    VkImage m_target = VK_NULL_HANDLE;
    VkDeviceMemory m_targetMemory = VK_NULL_HANDLE;
    uint32_t m_targetWidth = 0;
    uint32_t m_targetHeight = 0;
    bool m_targetWritten = false;

    Source m_sources[XrProtocol::kBufferCount];
    uint32_t m_sourceWidth = 0;
    uint32_t m_sourceHeight = 0;
    int m_current = -1;
    int m_held = -1;
    uint32_t m_packing = XrProtocol::Mono;
    uint32_t m_swapped = 0;
    uint32_t m_presented = 0;
    float m_eyeAspect = kDefaultAspect;
    int m_submitFailures = 0;
  };

  uint32_t Presenter::memoryType(uint32_t bits, VkMemoryPropertyFlags flags) const
  {
    for (uint32_t i = 0; i < m_memory.memoryTypeCount; ++i)
    {
      if (!(bits & (1u << i)))
        continue;
      if ((m_memory.memoryTypes[i].propertyFlags & flags) == flags)
        return i;
    }
    return UINT32_MAX;
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

  std::vector<const char*> Presenter::wantedInstanceExtensions()
  {
    std::vector<const char*> wanted;

    if (m_ovr.compositor && m_ovr.compositor->GetVulkanInstanceExtensionsRequired)
    {
      const uint32_t size = m_ovr.compositor->GetVulkanInstanceExtensionsRequired(nullptr, 0);
      if (size > 1)
      {
        m_runtimeExtensions.assign(size, '\0');
        m_ovr.compositor->GetVulkanInstanceExtensionsRequired(&m_runtimeExtensions[0], size);
        m_runtimeExtensions.resize(std::strlen(m_runtimeExtensions.c_str()));
        note("pmp-xr-host: the compositor wants instance extensions %s",
             m_runtimeExtensions.c_str());

        for (char& c : m_runtimeExtensions)
          if (c == ' ')
            c = '\0';

        const char* p = m_runtimeExtensions.c_str();
        const char* end = p + m_runtimeExtensions.size();
        while (p < end)
        {
          if (*p)
            wanted.push_back(p);
          p += std::strlen(p) + 1;
        }
      }
    }
    else
    {
      note("pmp-xr-host: the compositor interface is unavailable, using the dmabuf set alone");
    }

    return wanted;
  }

  std::vector<const char*> Presenter::wantedDeviceExtensions()
  {
    std::vector<const char*> wanted = {VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
                                       VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
                                       VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
                                       VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME,
                                       VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME};

    if (m_ovr.compositor && m_ovr.compositor->GetVulkanDeviceExtensionsRequired)
    {
      const uint32_t size = m_ovr.compositor->GetVulkanDeviceExtensionsRequired(
        reinterpret_cast<struct VkPhysicalDevice_T*>(m_physical), nullptr, 0);
      if (size > 1)
      {
        m_runtimeDeviceExtensions.assign(size, '\0');
        m_ovr.compositor->GetVulkanDeviceExtensionsRequired(
          reinterpret_cast<struct VkPhysicalDevice_T*>(m_physical), &m_runtimeDeviceExtensions[0],
          size);
        m_runtimeDeviceExtensions.resize(std::strlen(m_runtimeDeviceExtensions.c_str()));
        note("pmp-xr-host: the compositor wants device extensions %s",
             m_runtimeDeviceExtensions.c_str());

        for (char& c : m_runtimeDeviceExtensions)
          if (c == ' ')
            c = '\0';

        const char* p = m_runtimeDeviceExtensions.c_str();
        const char* end = p + m_runtimeDeviceExtensions.size();
        while (p < end)
        {
          if (*p)
            wanted.push_back(p);
          p += std::strlen(p) + 1;
        }
      }
    }

    return wanted;
  }

  bool Presenter::createDevice()
  {
    std::vector<const char*> instanceWanted = wantedInstanceExtensions();

    uint32_t availableCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, nullptr);
    std::vector<VkExtensionProperties> available(availableCount);
    if (availableCount > 0)
      vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, available.data());

    std::vector<const char*> instanceExtensions;
    for (const char* name : instanceWanted)
    {
      bool found = false;
      for (const VkExtensionProperties& property : available)
        if (std::strcmp(property.extensionName, name) == 0)
          found = true;
      if (found)
        instanceExtensions.push_back(name);
      else
        note("pmp-xr-host: instance extension %s is not offered, going on without it", name);
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "pmp-xr-host";
    appInfo.applicationVersion = 1;
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &appInfo;
    instanceInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
    instanceInfo.ppEnabledExtensionNames =
      instanceExtensions.empty() ? nullptr : instanceExtensions.data();

    if (!checkVk(vkCreateInstance(&instanceInfo, nullptr, &m_vkInstance), "vulkan instance"))
      return false;

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_vkInstance, &deviceCount, nullptr);
    if (deviceCount == 0)
    {
      note("pmp-xr-host: no vulkan device");
      return false;
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_vkInstance, &deviceCount, devices.data());

    uint64_t wantedDevice = 0;
    if (m_ovr.system && m_ovr.system->GetOutputDevice)
      m_ovr.system->GetOutputDevice(&wantedDevice, ETextureType_TextureType_Vulkan,
                                    reinterpret_cast<struct VkInstance_T*>(m_vkInstance));

    m_physical = devices[0];
    if (wantedDevice != 0)
    {
      for (VkPhysicalDevice device : devices)
      {
        if (reinterpret_cast<uint64_t>(device) == wantedDevice)
        {
          m_physical = device;
          break;
        }
      }
    }

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_physical, &properties);
    m_maxDimension = properties.limits.maxImageDimension2D;
    note("pmp-xr-host: vulkan device %s, images up to %u", properties.deviceName, m_maxDimension);

    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physical, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physical, &familyCount, families.data());

    bool haveFamily = false;
    for (uint32_t i = 0; i < familyCount; ++i)
    {
      if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
      {
        m_queueFamily = i;
        haveFamily = true;
        break;
      }
    }
    if (!haveFamily)
    {
      note("pmp-xr-host: no graphics queue family");
      return false;
    }

    std::vector<const char*> deviceWanted = wantedDeviceExtensions();

    uint32_t deviceAvailableCount = 0;
    vkEnumerateDeviceExtensionProperties(m_physical, nullptr, &deviceAvailableCount, nullptr);
    std::vector<VkExtensionProperties> deviceAvailable(deviceAvailableCount);
    if (deviceAvailableCount > 0)
      vkEnumerateDeviceExtensionProperties(m_physical, nullptr, &deviceAvailableCount,
                                           deviceAvailable.data());

    std::vector<const char*> deviceExtensions;
    for (const char* name : deviceWanted)
    {
      bool already = false;
      for (const char* kept : deviceExtensions)
        if (std::strcmp(kept, name) == 0)
          already = true;
      if (already)
        continue;

      bool found = false;
      for (const VkExtensionProperties& property : deviceAvailable)
        if (std::strcmp(property.extensionName, name) == 0)
          found = true;
      if (found)
        deviceExtensions.push_back(name);
      else
        note("pmp-xr-host: device extension %s is not offered, going on without it", name);
    }

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = m_queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    deviceInfo.ppEnabledExtensionNames =
      deviceExtensions.empty() ? nullptr : deviceExtensions.data();

    if (!checkVk(vkCreateDevice(m_physical, &deviceInfo, nullptr, &m_device), "vulkan device"))
      return false;

    vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);
    vkGetPhysicalDeviceMemoryProperties(m_physical, &m_memory);

    m_getMemoryFdProperties = reinterpret_cast<PFN_vkGetMemoryFdPropertiesKHR>(
      vkGetInstanceProcAddr(m_vkInstance, "vkGetMemoryFdPropertiesKHR"));
    if (!m_getMemoryFdProperties)
    {
      note("pmp-xr-host: vkGetMemoryFdPropertiesKHR missing");
      return false;
    }

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_queueFamily;
    if (!checkVk(vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_pool), "command pool"))
      return false;

    VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocateInfo.commandPool = m_pool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;
    if (!checkVk(vkAllocateCommandBuffers(m_device, &allocateInfo, &m_command), "command buffer"))
      return false;

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (!checkVk(vkCreateFence(m_device, &fenceInfo, nullptr, &m_fence), "fence"))
      return false;

    return true;
  }

  bool Presenter::createOverlay()
  {
    struct VR_IVROverlay_FnTable* ovl = m_ovr.overlay;

    EVROverlayError error = ovl->CreateOverlay(kOverlayKey, kOverlayName, &m_overlay);
    if (error != EVROverlayError_VROverlayError_None || m_overlay == k_ulOverlayHandleInvalid)
    {
      if (error == EVROverlayError_VROverlayError_KeyInUse)
      {
        ovl->FindOverlay(kOverlayKey, &m_overlay);
        note("pmp-xr-host: an overlay with our key already existed, reusing it");
      }
      if (m_overlay == k_ulOverlayHandleInvalid)
      {
        note("pmp-xr-host: the overlay was refused (%d)", static_cast<int>(error));
        return false;
      }
    }

    ovl->SetOverlayFlag(m_overlay, VROverlayFlags_SideBySide_Parallel, true);
    ovl->SetOverlayFlag(m_overlay, VROverlayFlags_IgnoreTextureAlpha, true);
    ovl->SetOverlayFlag(m_overlay, VROverlayFlags_SortWithNonSceneOverlays, true);
    ovl->SetOverlayTextureColorSpace(m_overlay, EColorSpace_ColorSpace_Gamma);
    ovl->SetOverlayWidthInMeters(m_overlay, kScreenWidth);
    if (ovl->SetOverlayTexelAspect)
      ovl->SetOverlayTexelAspect(m_overlay, 1.0f);
    ovl->SetOverlaySortOrder(m_overlay, kSortOrder);
    ovl->SetOverlayAlpha(m_overlay, 1.0f);

    placeScreen();

    error = ovl->ShowOverlay(m_overlay);
    if (error != EVROverlayError_VROverlayError_None)
    {
      note("pmp-xr-host: the overlay would not show (%d)", static_cast<int>(error));
      return false;
    }

    note("pmp-xr-host: overlay %llu ready, side by side, %.2f m wide",
         static_cast<unsigned long long>(m_overlay), kScreenWidth);
    return true;
  }

  bool Presenter::checkRowPitch(VkImage image, uint32_t stride) const
  {
    VkImageSubresource subresource{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
    VkSubresourceLayout layout{};
    vkGetImageSubresourceLayout(m_device, image, &subresource, &layout);

    if (layout.rowPitch == stride)
      return true;

    note("pmp-xr-host: the shared buffer has %u bytes a row but this device wants %llu", stride,
         static_cast<unsigned long long>(layout.rowPitch));
    return false;
  }

  bool Presenter::checkSize(int fd, VkDeviceSize needed,
                            const XrProtocol::Message& message) const
  {
    const off_t real = lseek(fd, 0, SEEK_END);
    if (real < 0)
    {
      note("pmp-xr-host: the shared buffer has no measurable size (%s)", std::strerror(errno));
      return true;
    }
    lseek(fd, 0, SEEK_SET);

    if (static_cast<VkDeviceSize>(real) >= needed)
      return true;

    note("pmp-xr-host: the shared buffer is %lld bytes but a %ux%u image needs %llu, so the "
         "layout this device picked does not match the one the buffer was made with",
         static_cast<long long>(real), message.width, message.height,
         static_cast<unsigned long long>(needed));
    return false;
  }

  bool Presenter::importBuffers(const XrProtocol::Message& message, const int* fds, int count)
  {
    if (count <= 0 || count > static_cast<int>(XrProtocol::kBufferCount))
    {
      note("pmp-xr-host: %d buffer descriptors is not usable", count);
      return false;
    }

    m_sourceWidth = message.width;
    m_sourceHeight = message.height;

    const bool stated = message.modifier != kModifierInvalid;

    note("pmp-xr-host: importing %ux%u stride %u offset %u modifier 0x%llx as %s",
         message.width, message.height, message.stride, message.offset,
         static_cast<unsigned long long>(message.modifier), stated ? "stated" : "linear");

    for (int i = 0; i < count; ++i)
    {
      Source& source = m_sources[i];
      source.fd = fds[i];

      VkSubresourceLayout planeLayout{};
      planeLayout.offset = message.offset;
      planeLayout.rowPitch = message.stride;

      VkImageDrmFormatModifierExplicitCreateInfoEXT modifierInfo{
        VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT};
      modifierInfo.drmFormatModifier = message.modifier;
      modifierInfo.drmFormatModifierPlaneCount = 1;
      modifierInfo.pPlaneLayouts = &planeLayout;

      VkExternalMemoryImageCreateInfo externalInfo{
        VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
      externalInfo.pNext = stated ? &modifierInfo : nullptr;
      externalInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

      VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
      imageInfo.pNext = &externalInfo;
      imageInfo.imageType = VK_IMAGE_TYPE_2D;
      imageInfo.format = m_sourceFormat;
      imageInfo.extent = {message.width, message.height, 1};
      imageInfo.mipLevels = 1;
      imageInfo.arrayLayers = 1;
      imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
      imageInfo.tiling =
        stated ? VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT : VK_IMAGE_TILING_LINEAR;
      imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
      imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

      if (!checkVk(vkCreateImage(m_device, &imageInfo, nullptr, &source.image), "import image"))
        return false;

      if (!stated && !checkRowPitch(source.image, message.stride))
        return false;

      VkMemoryRequirements requirements{};
      vkGetImageMemoryRequirements(m_device, source.image, &requirements);

      if (!checkSize(source.fd, requirements.size, message))
        return false;

      VkMemoryFdPropertiesKHR fdProperties{VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};
      if (!checkVk(m_getMemoryFdProperties(m_device,
                                           VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
                                           source.fd, &fdProperties),
                   "dmabuf properties"))
        return false;

      const uint32_t type = memoryType(requirements.memoryTypeBits & fdProperties.memoryTypeBits,
                                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      const uint32_t chosen =
        type != UINT32_MAX
          ? type
          : memoryType(requirements.memoryTypeBits & fdProperties.memoryTypeBits, 0);
      if (chosen == UINT32_MAX)
      {
        note("pmp-xr-host: no memory type accepts the shared buffer");
        return false;
      }

      VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
      dedicated.image = source.image;

      VkImportMemoryFdInfoKHR importInfo{VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR};
      importInfo.pNext = &dedicated;
      importInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
      importInfo.fd = source.fd;

      VkMemoryAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      allocateInfo.pNext = &importInfo;
      allocateInfo.allocationSize = requirements.size;
      allocateInfo.memoryTypeIndex = chosen;

      if (!checkVk(vkAllocateMemory(m_device, &allocateInfo, nullptr, &source.memory),
                   "import memory"))
        return false;

      source.fd = -1;

      if (!checkVk(vkBindImageMemory(m_device, source.image, source.memory, 0), "bind import"))
        return false;

      source.ready = true;
    }

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(m_command, &beginInfo);

    std::vector<VkImageMemoryBarrier> barriers;
    for (int i = 0; i < count; ++i)
    {
      VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT;
      barrier.dstQueueFamilyIndex = m_queueFamily;
      barrier.image = m_sources[i].image;
      barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      barriers.push_back(barrier);
    }

    vkCmdPipelineBarrier(m_command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                         static_cast<uint32_t>(barriers.size()), barriers.data());
    vkEndCommandBuffer(m_command);

    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &m_command;
    vkResetFences(m_device, 1, &m_fence);
    vkQueueSubmit(m_queue, 1, &submit, m_fence);
    vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX);

    note("pmp-xr-host: imported %d shared buffers of %ux%u", count, message.width,
         message.height);
    return true;
  }

  bool Presenter::ensureTarget()
  {
    uint32_t rectWidth = m_sourceWidth;
    uint32_t rectHeight = m_sourceHeight;
    if (m_packing == XrProtocol::SideBySide)
      rectWidth /= 2;
    else if (m_packing == XrProtocol::OverUnder)
      rectHeight /= 2;

    if (rectWidth < 2 || rectHeight < 2)
      return false;

    float aspect = m_eyeAspect;
    if (!(aspect > 0.1f && aspect < 10.0f))
      aspect = kDefaultAspect;

    uint32_t eyeHeight = rectHeight;
    const long fromWidth = std::lround(rectWidth / aspect);
    if (fromWidth > static_cast<long>(eyeHeight))
      eyeHeight = static_cast<uint32_t>(fromWidth);

    long fromHeight = std::lround(aspect * eyeHeight);
    if (fromHeight < 2)
      fromHeight = 2;
    uint32_t eyeWidth = static_cast<uint32_t>(fromHeight);

    while ((eyeWidth * 2 > m_maxDimension || eyeHeight > m_maxDimension) && eyeHeight > 2)
    {
      eyeWidth /= 2;
      eyeHeight /= 2;
    }

    eyeWidth &= ~1u;
    eyeHeight &= ~1u;
    if (eyeWidth < 2 || eyeHeight < 2)
      return false;

    const uint32_t width = eyeWidth * 2;
    const uint32_t height = eyeHeight;

    if (m_target != VK_NULL_HANDLE && width == m_targetWidth && height == m_targetHeight)
      return true;

    if (m_target != VK_NULL_HANDLE)
    {
      vkDeviceWaitIdle(m_device);
      if (m_overlay != k_ulOverlayHandleInvalid && m_ovr.overlay)
        m_ovr.overlay->ClearOverlayTexture(m_overlay);
      vkDestroyImage(m_device, m_target, nullptr);
      vkFreeMemory(m_device, m_targetMemory, nullptr);
      m_target = VK_NULL_HANDLE;
      m_targetMemory = VK_NULL_HANDLE;
      m_targetWritten = false;
    }

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = m_sourceFormat;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (!checkVk(vkCreateImage(m_device, &imageInfo, nullptr, &m_target), "overlay image"))
      return false;

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(m_device, m_target, &requirements);

    const uint32_t chosen =
      memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (chosen == UINT32_MAX)
    {
      note("pmp-xr-host: no device memory for the overlay image");
      return false;
    }

    VkMemoryAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = chosen;
    if (!checkVk(vkAllocateMemory(m_device, &allocateInfo, nullptr, &m_targetMemory),
                 "overlay memory"))
      return false;

    if (!checkVk(vkBindImageMemory(m_device, m_target, m_targetMemory, 0), "bind overlay"))
      return false;

    m_targetWidth = width;
    m_targetHeight = height;

    if (m_ovr.overlay && m_ovr.overlay->SetOverlayMouseScale)
    {
      HmdVector2_t scale{};
      scale.v[0] = static_cast<float>(width);
      scale.v[1] = static_cast<float>(height);
      m_ovr.overlay->SetOverlayMouseScale(m_overlay, &scale);
    }

    note("pmp-xr-host: overlay texture %ux%u, %ux%u per eye, aspect %.3f, %.2fx%.2f m",
         width, height, eyeWidth, eyeHeight, aspect, kScreenWidth, kScreenWidth / aspect);
    return true;
  }

  bool Presenter::headPose(float position[3], float forward[3]) const
  {
    if (!m_ovr.system || !m_ovr.system->GetDeviceToAbsoluteTrackingPose)
      return false;

    struct TrackedDevicePose_t poses[k_unTrackedDeviceIndex_Hmd + 1] = {};
    m_ovr.system->GetDeviceToAbsoluteTrackingPose(
      ETrackingUniverseOrigin_TrackingUniverseStanding, 0.0f, poses,
      k_unTrackedDeviceIndex_Hmd + 1);

    const struct TrackedDevicePose_t& pose = poses[k_unTrackedDeviceIndex_Hmd];
    if (!pose.bPoseIsValid || !pose.bDeviceIsConnected)
      return false;

    const HmdMatrix34_t& m = pose.mDeviceToAbsoluteTracking;
    position[0] = m.m[0][3];
    position[1] = m.m[1][3];
    position[2] = m.m[2][3];

    float look[3] = {-m.m[0][2], 0.0f, -m.m[2][2]};
    const float length = std::sqrt(look[0] * look[0] + look[2] * look[2]);
    if (length < 0.0001f)
    {
      forward[0] = 0.0f;
      forward[1] = 0.0f;
      forward[2] = -1.0f;
      return true;
    }

    forward[0] = look[0] / length;
    forward[1] = 0.0f;
    forward[2] = look[2] / length;
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
    if (ovl->GetOverlayPreCurvePitch)
      ovl->GetOverlayPreCurvePitch(handle, &out.pitch);
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

    out.placed = ovl->GetOverlayTransformAbsolute &&
                 ovl->GetOverlayTransformAbsolute(handle, &out.origin, &out.transform) ==
                   EVROverlayError_VROverlayError_None;

    if (!out.placed && ovl->GetTransformForOverlayCoordinates)
    {
      HmdVector2_t centre{};
      out.origin = ETrackingUniverseOrigin_TrackingUniverseStanding;
      out.placed = ovl->GetTransformForOverlayCoordinates(handle, out.origin, centre,
                                                          &out.transform) ==
                   EVROverlayError_VROverlayError_None;
    }

    return true;
  }

  bool Presenter::dockable(const Neighbour& candidate) const
  {
    if (candidate.handle == m_overlay || !candidate.placed || !candidate.visible)
      return false;

    if (candidate.width < kDockMinWidth || candidate.width > kDockMaxWidth)
      return false;

    if (std::strcmp(candidate.key, kOverlayKey) == 0)
      return false;

    static const char* const furniture[] = {"system.systemui",     "system.toast",
                                            "system.pointer",      "system.keyboard",
                                            "system.notification", "system.vrdashboard",
                                            "system.vrmonitor",    "system.generated."};

    for (const char* piece : furniture)
      if (std::strncmp(candidate.key, piece, std::strlen(piece)) == 0)
        return false;

    return true;
  }

  int Presenter::dockScore(const Neighbour& candidate) const
  {
    int score = 0;

    if (std::strstr(candidate.key, ".window."))
      score += 100;
    if (std::strstr(candidate.key, "gamescope"))
      score += 50;
    if (std::strcmp(candidate.name, kOverlayName) == 0)
      score += 25;
    if (std::strstr(candidate.key, "steam"))
      score += 10;

    return score;
  }

  void Presenter::survey()
  {
    struct VR_IVROverlay_FnTable* ovl = m_ovr.overlay;
    if (!ovl || !ovl->GetOverlayKey || m_overlay == k_ulOverlayHandleInvalid)
      return;

    const int64_t now = nowMs();

    if (!m_surveyRunning)
    {
      const int gap = m_surveyRounds < kSurveyLoudRounds ? kSurveyGapMs : kSurveyPatientGapMs;
      if (m_surveyDone && now - m_surveyAt < gap)
        return;

      m_surveyRunning = true;
      m_surveyDone = false;
      m_surveyAt = now;
      m_haveBest = false;
      m_scanSeen = 0;
      m_scanProbes = 0;
      m_scanIndex = 0;

      const uint32_t ours = static_cast<uint32_t>(m_overlay >> 32);
      m_scanGeneration = (ours > m_topGeneration ? ours : m_topGeneration) + kScanAhead;

      if (!m_environmentDescribed)
        describeEnvironment();

      const char* named = std::getenv("STEAM_GAMESCOPE_VROVERLAY_TARGET");
      if (named && *named && ovl->FindOverlay)
      {
        VROverlayHandle_t handle = k_ulOverlayHandleInvalid;
        Neighbour target;
        if (ovl->FindOverlay(const_cast<char*>(named), &handle) ==
              EVROverlayError_VROverlayError_None &&
            handle != k_ulOverlayHandleInvalid && readNeighbour(handle, target))
        {
          note("pmp-xr-host: the session names \"%s\" as the window to render into", named);
          if (dockable(target))
          {
            m_best = target;
            m_haveBest = true;
          }
        }
      }

      if (m_surveyRounds < kSurveyLoudRounds)
        note("pmp-xr-host: looking for the window to dock to, generation %u down to 0",
             m_scanGeneration);
    }

    const int64_t slice = nowMs();

    for (int budget = 0; budget < kScanBudget; ++budget)
    {
      if ((budget & 63) == 63 && nowMs() - slice >= kScanSliceMs)
        return;

      ++m_scanProbes;

      const VROverlayHandle_t handle =
        (static_cast<VROverlayHandle_t>(m_scanGeneration) << 32) | m_scanIndex;

      bool last = false;
      if (++m_scanIndex >= kScanIndexes)
      {
        m_scanIndex = 0;
        if (m_scanGeneration == 0)
          last = true;
        else
          --m_scanGeneration;
      }

      Neighbour found;
      const bool live = handle != m_overlay && readNeighbour(handle, found);

      if (last)
      {
        if (live && dockable(found) && (!m_haveBest || dockScore(found) > dockScore(m_best)))
        {
          m_best = found;
          m_haveBest = true;
        }
        finishSurvey();
        return;
      }

      if (!live)
        continue;

      ++m_scanSeen;

      const uint32_t generation = static_cast<uint32_t>(handle >> 32);
      if (generation > m_topGeneration)
        m_topGeneration = generation;

      if (m_surveyRounds < kSurveyLoudRounds)
        note("pmp-xr-host: overlay %llu \"%s\" named \"%s\", %.2f m wide, texture %ux%u, %s, "
             "flags 0x%x, sort %u, transform %d, curve %.3f, pid %u, at %.2f %.2f %.2f%s",
             static_cast<unsigned long long>(handle), found.key, found.name, found.width,
             found.textureWidth, found.textureHeight, found.visible ? "showing" : "hidden",
             found.flags, found.sortOrder, found.transformType, found.curvature, found.pid,
             found.transform.m[0][3], found.transform.m[1][3], found.transform.m[2][3],
             found.placed ? "" : " (no transform)");

      if (!dockable(found))
        continue;

      if (!m_haveBest || dockScore(found) > dockScore(m_best) ||
          (dockScore(found) == dockScore(m_best) && found.width > m_best.width))
      {
        m_best = found;
        m_haveBest = true;
      }
    }
  }

  void Presenter::finishSurvey()
  {
    m_surveyRunning = false;
    m_surveyDone = true;
    m_surveyAt = nowMs();
    ++m_surveyRounds;

    if (m_haveBest)
    {
      note("pmp-xr-host: %d of %d handles are live overlays", m_scanSeen, m_scanProbes);
      adoptDock(m_best);
      return;
    }

    if (m_surveyRounds <= kSurveyLoudRounds)
      note("pmp-xr-host: %d of %d handles are live overlays and none of them is a window to "
           "dock to; the screen stays where it was put",
           m_scanSeen, m_scanProbes);
  }

  void Presenter::adoptDock(const Neighbour& chosen)
  {
    m_dock = chosen.handle;
    m_dockKey.assign(chosen.key);
    m_docked = true;
    m_dockLosses = 0;
    m_dockedWidth = 0.0f;

    float head[3] = {0.0f, kDefaultEyeHeight, 0.0f};
    float look[3] = {0.0f, 0.0f, -1.0f};
    headPose(head, look);

    float facing = 0.0f;
    for (int axis = 0; axis < 3; ++axis)
      facing += chosen.transform.m[axis][2] * (head[axis] - chosen.transform.m[axis][3]);

    m_dockNudge = facing < 0.0f ? -kDockNudge : kDockNudge;

    note("pmp-xr-host: docking the screen to \"%s\" %llu, %.2f m wide, texture %ux%u, at %.2f "
         "%.2f %.2f",
         m_dockKey.c_str(), static_cast<unsigned long long>(chosen.handle), chosen.width,
         chosen.textureWidth, chosen.textureHeight, chosen.transform.m[0][3],
         chosen.transform.m[1][3], chosen.transform.m[2][3]);

    if (chosen.textureWidth > 0 && chosen.textureHeight > 0)
    {
      const float windowAspect =
        static_cast<float>(chosen.textureWidth) / static_cast<float>(chosen.textureHeight);
      if (std::fabs(windowAspect - m_eyeAspect) > 0.05f)
        note("pmp-xr-host: the window is %.3f wide for its height and each eye is %.3f, so the "
             "screen will not cover it exactly",
             windowAspect, m_eyeAspect);
    }

    applyDock(chosen, true);
  }

  void Presenter::applyDock(const Neighbour& window, bool force)
  {
    struct VR_IVROverlay_FnTable* ovl = m_ovr.overlay;
    if (!ovl || !ovl->SetOverlayTransformAbsolute)
      return;

    HmdMatrix34_t placed = window.transform;
    for (int row = 0; row < 3; ++row)
      placed.m[row][3] += window.transform.m[row][2] * m_dockNudge;

    bool moved = force;
    for (int row = 0; row < 3 && !moved; ++row)
      for (int column = 0; column < 4 && !moved; ++column)
        if (std::fabs(placed.m[row][column] - m_dockedTransform.m[row][column]) > 0.0005f)
          moved = true;

    const bool resized = force || std::fabs(window.width - m_dockedWidth) > 0.001f;
    const bool reshaped = std::fabs(window.curvature - m_dockedCurvature) > 0.001f ||
                          std::fabs(window.pitch - m_dockedPitch) > 0.001f ||
                          window.sortOrder + 1 != m_dockedSortOrder;

    if (!moved && !resized && !reshaped)
      return;

    if (resized && ovl->SetOverlayWidthInMeters)
      ovl->SetOverlayWidthInMeters(m_overlay, window.width);

    if (std::fabs(window.curvature - m_dockedCurvature) > 0.001f && ovl->SetOverlayCurvature)
    {
      ovl->SetOverlayCurvature(m_overlay, window.curvature);
      m_dockedCurvature = window.curvature;
    }

    if (std::fabs(window.pitch - m_dockedPitch) > 0.001f && ovl->SetOverlayPreCurvePitch)
    {
      ovl->SetOverlayPreCurvePitch(m_overlay, window.pitch);
      m_dockedPitch = window.pitch;
    }

    const uint32_t above = window.sortOrder + 1;
    if (above != m_dockedSortOrder && ovl->SetOverlaySortOrder)
    {
      ovl->SetOverlaySortOrder(m_overlay, above);
      m_dockedSortOrder = above;
    }

    const EVROverlayError error =
      ovl->SetOverlayTransformAbsolute(m_overlay, window.origin, &placed);
    if (error != EVROverlayError_VROverlayError_None)
    {
      note("pmp-xr-host: the screen would not follow the window (%d)", static_cast<int>(error));
      return;
    }

    m_dockedTransform = placed;
    m_dockedWidth = window.width;
    m_screenPlaced = true;
  }

  void Presenter::trackDock()
  {
    Neighbour window;
    if (!readNeighbour(m_dock, window) || !window.placed)
    {
      if (++m_dockLosses < kDockLossLimit)
        return;

      note("pmp-xr-host: the window the screen was docked to is gone, looking for another");
      m_docked = false;
      m_dock = k_ulOverlayHandleInvalid;
      m_dockLosses = 0;
      m_surveyDone = false;
      m_surveyRunning = false;
      m_surveyRounds = 0;
      return;
    }

    m_dockLosses = 0;

    if (window.visible != m_dockVisible)
    {
      m_dockVisible = window.visible;
      updateVisibility();
    }

    applyDock(window, false);
  }

  void Presenter::setVisible(bool visible)
  {
    m_appVisible = visible;
    updateVisibility();
  }

  void Presenter::updateVisibility()
  {
    struct VR_IVROverlay_FnTable* ovl = m_ovr.overlay;
    if (!ovl || m_overlay == k_ulOverlayHandleInvalid)
      return;

    const bool wanted = m_appVisible && m_dockVisible;
    if (wanted == m_shown)
      return;

    m_shown = wanted;
    if (wanted)
      ovl->ShowOverlay(m_overlay);
    else if (ovl->HideOverlay)
      ovl->HideOverlay(m_overlay);

    note("pmp-xr-host: the screen is now %s%s", wanted ? "showing" : "out of the way",
         m_dockVisible ? "" : " because the window it docks to is not showing");
  }

  void Presenter::send(XrProtocol::Type type, uint32_t index)
  {
    if (m_socket < 0)
      return;
    XrProtocol::Message message = XrProtocol::make(type);
    message.index = index;
    if (::send(m_socket, &message, sizeof(message), MSG_NOSIGNAL | MSG_DONTWAIT) < 0 &&
        errno != EAGAIN && errno != EWOULDBLOCK)
      note("pmp-xr-host: the app connection broke while answering");
  }

  bool Presenter::readSocket()
  {
    while (true)
    {
      XrProtocol::Message message{};
      int fds[XrProtocol::kBufferCount] = {-1, -1, -1};

      struct iovec iov = {&message, sizeof(message)};
      char control[CMSG_SPACE(sizeof(int) * XrProtocol::kBufferCount)] = {0};

      struct msghdr header = {};
      header.msg_iov = &iov;
      header.msg_iovlen = 1;
      header.msg_control = control;
      header.msg_controllen = sizeof(control);

      const ssize_t got = recvmsg(m_socket, &header, MSG_DONTWAIT);
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

      int count = 0;
      for (struct cmsghdr* cmsg = CMSG_FIRSTHDR(&header); cmsg; cmsg = CMSG_NXTHDR(&header, cmsg))
      {
        if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS)
          continue;
        count = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
        if (count > static_cast<int>(XrProtocol::kBufferCount))
          count = XrProtocol::kBufferCount;
        std::memcpy(fds, CMSG_DATA(cmsg), count * sizeof(int));
      }

      if (got != static_cast<ssize_t>(sizeof(message)) || !XrProtocol::valid(message))
      {
        note("pmp-xr-host: a message of %zd bytes was not understood", got);
        for (int i = 0; i < count; ++i)
          close(fds[i]);
        continue;
      }

      switch (message.type)
      {
        case XrProtocol::Buffers:
          if (!importBuffers(message, fds, count))
          {
            for (int i = 0; i < count; ++i)
              if (fds[i] >= 0)
                close(fds[i]);
            send(XrProtocol::Failed, 0);
            return false;
          }
          send(XrProtocol::Started, 0);
          break;

        case XrProtocol::Frame:
          if (message.index < XrProtocol::kBufferCount && m_sources[message.index].ready)
          {
            if (m_held >= 0 && m_held != static_cast<int>(message.index))
              send(XrProtocol::Release, static_cast<uint32_t>(m_held));
            m_held = static_cast<int>(message.index);
            m_current = static_cast<int>(message.index);
            m_packing = message.packing;
            m_swapped = message.swapped;
            if (message.eyeAspect > 0.1f && message.eyeAspect < 10.0f)
              m_eyeAspect = message.eyeAspect;
          }
          break;

        case XrProtocol::Show:
          setVisible(true);
          break;

        case XrProtocol::Hide:
          setVisible(false);
          break;

        case XrProtocol::Stop:
          note("pmp-xr-host: the app asked to stop");
          return false;

        default:
          break;
      }
    }
  }

  void Presenter::placeScreen()
  {
    struct VR_IVROverlay_FnTable* ovl = m_ovr.overlay;
    if (!ovl || !ovl->SetOverlayTransformAbsolute)
      return;

    ++m_placeAttempts;

    float position[3] = {0.0f, kDefaultEyeHeight, 0.0f};
    float forward[3] = {0.0f, 0.0f, -1.0f};
    const bool tracked = headPose(position, forward);
    if (!tracked && m_placeAttempts < kPlaceAttempts)
      return;

    HmdMatrix34_t transform{};
    const float up[3] = {0.0f, 1.0f, 0.0f};
    const float back[3] = {-forward[0], 0.0f, -forward[2]};
    const float right[3] = {back[2], 0.0f, -back[0]};

    for (int row = 0; row < 3; ++row)
    {
      transform.m[row][0] = right[row];
      transform.m[row][1] = up[row];
      transform.m[row][2] = back[row];
      transform.m[row][3] = position[row] + forward[row] * kScreenDistance;
    }

    const EVROverlayError error = ovl->SetOverlayTransformAbsolute(
      m_overlay, ETrackingUniverseOrigin_TrackingUniverseStanding, &transform);
    if (error != EVROverlayError_VROverlayError_None)
    {
      note("pmp-xr-host: the screen would not stay put (%d)", static_cast<int>(error));
      return;
    }

    m_screenPlaced = true;
    note("pmp-xr-host: screen fixed at %.2f %.2f %.2f, %.2f m from where the headset was "
         "looking%s",
         transform.m[0][3], transform.m[1][3], transform.m[2][3], kScreenDistance,
         tracked ? "" : " (no pose, placed ahead of the standing origin)");
  }

  bool Presenter::blitEyes()
  {
    const Source& source = m_sources[m_current];
    const int32_t eyeWidth = static_cast<int32_t>(m_targetWidth / 2);
    const int32_t eyeHeight = static_cast<int32_t>(m_targetHeight);

    VkImageBlit regions[2] = {};
    for (int eye = 0; eye < 2; ++eye)
    {
      int32_t x0 = 0;
      int32_t y0 = 0;
      int32_t x1 = static_cast<int32_t>(m_sourceWidth);
      int32_t y1 = static_cast<int32_t>(m_sourceHeight);

      int which = eye;
      if (m_swapped)
        which = 1 - which;

      if (m_packing == XrProtocol::SideBySide)
      {
        const int32_t half = x1 / 2;
        x0 = which == 0 ? 0 : half;
        x1 = which == 0 ? half : static_cast<int32_t>(m_sourceWidth);
      }
      else if (m_packing == XrProtocol::OverUnder)
      {
        const int32_t half = y1 / 2;
        y0 = which == 0 ? 0 : half;
        y1 = which == 0 ? half : static_cast<int32_t>(m_sourceHeight);
      }

      regions[eye].srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
      regions[eye].srcOffsets[0] = {x0, y0, 0};
      regions[eye].srcOffsets[1] = {x1, y1, 1};
      regions[eye].dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
      regions[eye].dstOffsets[0] = {eye == 0 ? 0 : eyeWidth, eyeHeight, 0};
      regions[eye].dstOffsets[1] = {eye == 0 ? eyeWidth : eyeWidth * 2, 0, 1};
    }

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(m_command, &beginInfo);

    VkImageMemoryBarrier acquire{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    acquire.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    acquire.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    acquire.srcQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT;
    acquire.dstQueueFamilyIndex = m_queueFamily;
    acquire.image = source.image;
    acquire.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    acquire.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    VkImageMemoryBarrier target{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    target.oldLayout = m_targetWritten ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                       : VK_IMAGE_LAYOUT_UNDEFINED;
    target.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    target.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    target.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    target.image = m_target;
    target.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    target.srcAccessMask = m_targetWritten ? VK_ACCESS_COLOR_ATTACHMENT_READ_BIT : 0;
    target.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    VkImageMemoryBarrier before[2] = {acquire, target};
    vkCmdPipelineBarrier(m_command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, before);

    vkCmdBlitImage(m_command, source.image, VK_IMAGE_LAYOUT_GENERAL, m_target,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 2, regions, VK_FILTER_LINEAR);

    VkImageMemoryBarrier release{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    release.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    release.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    release.srcQueueFamilyIndex = m_queueFamily;
    release.dstQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT;
    release.image = source.image;
    release.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    release.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    VkImageMemoryBarrier present{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    present.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    present.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    present.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    present.image = m_target;
    present.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    present.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    present.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

    VkImageMemoryBarrier after[2] = {release, present};
    vkCmdPipelineBarrier(m_command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr,
                         2, after);

    vkEndCommandBuffer(m_command);

    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &m_command;
    vkResetFences(m_device, 1, &m_fence);
    if (!checkVk(vkQueueSubmit(m_queue, 1, &submit, m_fence), "blit submit"))
      return false;
    vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX);
    m_targetWritten = true;
    return true;
  }

  void Presenter::frame()
  {
    if (m_ovr.overlay->WaitFrameSync)
      m_ovr.overlay->WaitFrameSync(kFrameSyncTimeoutMs);
    else
      sleepMs(11);

    if (m_current < 0 || !m_sources[m_current].ready)
    {
      sleepMs(5);
      return;
    }

    if (!ensureTarget())
    {
      sleepMs(10);
      return;
    }

    if (m_docked)
      trackDock();
    else
      survey();

    if (!m_docked && !m_screenPlaced)
      placeScreen();

    if (!blitEyes())
      return;

    VRVulkanTextureData_t data{};
    data.m_nImage = reinterpret_cast<uint64_t>(m_target);
    data.m_pDevice = reinterpret_cast<struct VkDevice_T*>(m_device);
    data.m_pPhysicalDevice = reinterpret_cast<struct VkPhysicalDevice_T*>(m_physical);
    data.m_pInstance = reinterpret_cast<struct VkInstance_T*>(m_vkInstance);
    data.m_pQueue = reinterpret_cast<struct VkQueue_T*>(m_queue);
    data.m_nQueueFamilyIndex = m_queueFamily;
    data.m_nWidth = m_targetWidth;
    data.m_nHeight = m_targetHeight;
    data.m_nFormat = m_sourceFormat;
    data.m_nSampleCount = 1;

    Texture_t texture{};
    texture.handle = &data;
    texture.eType = ETextureType_TextureType_Vulkan;
    texture.eColorSpace = EColorSpace_ColorSpace_Gamma;

    const EVROverlayError error = m_ovr.overlay->SetOverlayTexture(m_overlay, &texture);
    if (error != EVROverlayError_VROverlayError_None)
    {
      if (m_submitFailures < 3)
        note("pmp-xr-host: the overlay texture was refused (%d)", static_cast<int>(error));
      ++m_submitFailures;
      return;
    }

    if (m_presented == 0)
    {
      note("pmp-xr-host: presenting %ux%u side by side", m_targetWidth, m_targetHeight);
      m_presented = 1;
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
    return startRuntime() && createDevice() && createOverlay();
  }

  int Presenter::run()
  {
    bool exit = false;
    while (!exit)
    {
      if (!readSocket())
        break;

      pollEvents(exit);
      if (exit)
        break;

      frame();
    }

    note("pmp-xr-host: presenter stopping");
    return 0;
  }

  void Presenter::teardown()
  {
    if (m_device != VK_NULL_HANDLE)
      vkDeviceWaitIdle(m_device);

    if (m_overlay != k_ulOverlayHandleInvalid && m_ovr.overlay)
    {
      m_ovr.overlay->ClearOverlayTexture(m_overlay);
      m_ovr.overlay->DestroyOverlay(m_overlay);
      m_overlay = k_ulOverlayHandleInvalid;
    }

    if (m_target != VK_NULL_HANDLE)
      vkDestroyImage(m_device, m_target, nullptr);
    if (m_targetMemory != VK_NULL_HANDLE)
      vkFreeMemory(m_device, m_targetMemory, nullptr);
    m_target = VK_NULL_HANDLE;
    m_targetMemory = VK_NULL_HANDLE;

    for (Source& source : m_sources)
    {
      if (source.image != VK_NULL_HANDLE)
        vkDestroyImage(m_device, source.image, nullptr);
      if (source.memory != VK_NULL_HANDLE)
        vkFreeMemory(m_device, source.memory, nullptr);
      if (source.fd >= 0)
        close(source.fd);
      source = Source();
    }

    if (m_fence != VK_NULL_HANDLE)
      vkDestroyFence(m_device, m_fence, nullptr);
    if (m_pool != VK_NULL_HANDLE)
      vkDestroyCommandPool(m_device, m_pool, nullptr);
    if (m_device != VK_NULL_HANDLE)
      vkDestroyDevice(m_device, nullptr);
    if (m_vkInstance != VK_NULL_HANDLE)
      vkDestroyInstance(m_vkInstance, nullptr);

    m_fence = VK_NULL_HANDLE;
    m_pool = VK_NULL_HANDLE;
    m_device = VK_NULL_HANDLE;
    m_vkInstance = VK_NULL_HANDLE;

    m_ovr.stop();
  }
}

int XrPresent::run(int socketFd)
{
  Presenter presenter(socketFd);
  if (!presenter.setup())
  {
    XrProtocol::Message message = XrProtocol::make(XrProtocol::Failed);
    ::send(socketFd, &message, sizeof(message), MSG_NOSIGNAL);
    return 1;
  }
  return presenter.run();
}

#else

int XrPresent::run(int)
{
  return 1;
}

#endif
