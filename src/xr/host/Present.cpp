#include "Present.h"

#ifdef PMP_XR_HOST_VULKAN

#include <vulkan/vulkan.h>

#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../XrProtocol.h"

namespace
{
  const uint32_t kDrmFormatAbgr8888 = 0x34324241;
  const uint64_t kDrmModifierLinear = 0;

  const float kScreenDistance = 2.0f;
  const float kScreenWidth = 2.4f;

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

  struct Source
  {
    int fd = -1;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    bool ready = false;
  };

  struct Eye
  {
    XrSwapchain swapchain = XR_NULL_HANDLE;
    std::vector<VkImage> images;
  };

  class Presenter
  {
  public:
    explicit Presenter(int socketFd) : m_socket(socketFd) {}
    ~Presenter() { teardown(); }

    bool setup();
    int run();

  private:
    bool createInstance();
    bool createDevice();
    bool createSession();
    bool createSwapchains();
    bool importBuffers(const XrProtocol::Message& message, const int* fds, int count);
    void teardown();

    bool readSocket();
    void send(XrProtocol::Type type, uint32_t index);

    void pollEvents(bool& exit);
    void frame();
    bool blit(int eye, uint32_t imageIndex);
    void placeScreen(XrTime time);

    uint32_t memoryType(uint32_t bits, VkMemoryPropertyFlags flags) const;

    int m_socket = -1;

    XrInstance m_instance = XR_NULL_HANDLE;
    XrSystemId m_system = XR_NULL_SYSTEM_ID;
    XrSession m_session = XR_NULL_HANDLE;
    XrSpace m_localSpace = XR_NULL_HANDLE;
    XrSpace m_viewSpace = XR_NULL_HANDLE;
    XrSessionState m_state = XR_SESSION_STATE_UNKNOWN;
    bool m_running = false;
    XrPosef m_screenPose{};
    bool m_screenPlaced = false;
    XrExtent2Df m_screenSize{kScreenWidth, kScreenWidth * 9.0f / 16.0f};

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

    int64_t m_swapchainFormat = 0;
    VkFormat m_sourceFormat = VK_FORMAT_R8G8B8A8_UNORM;
    uint32_t m_eyeWidth = 0;
    uint32_t m_eyeHeight = 0;
    Eye m_eyes[2];

    Source m_sources[XrProtocol::kBufferCount];
    uint32_t m_sourceWidth = 0;
    uint32_t m_sourceHeight = 0;
    int m_current = -1;
    int m_held = -1;
    uint32_t m_packing = XrProtocol::Mono;
    uint32_t m_swapped = 0;
    uint32_t m_presented = 0;
  };

  bool check(XrResult result, const char* stage)
  {
    if (XR_SUCCEEDED(result))
      return true;
    note("pmp-xr-host: %s failed (%d)", stage, static_cast<int>(result));
    return false;
  }

  bool checkVk(VkResult result, const char* stage)
  {
    if (result == VK_SUCCESS)
      return true;
    note("pmp-xr-host: %s failed (%d)", stage, static_cast<int>(result));
    return false;
  }

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

  bool Presenter::createInstance()
  {
    const char* extensions[] = {XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME};

    XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
    createInfo.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    std::strncpy(createInfo.applicationInfo.applicationName, "Plex Media Player",
                 XR_MAX_APPLICATION_NAME_SIZE - 1);
    std::strncpy(createInfo.applicationInfo.engineName, "pmp-xr-host",
                 XR_MAX_ENGINE_NAME_SIZE - 1);
    createInfo.applicationInfo.applicationVersion = 1;
    createInfo.applicationInfo.engineVersion = 1;
    createInfo.enabledExtensionCount = 1;
    createInfo.enabledExtensionNames = extensions;

    if (!check(xrCreateInstance(&createInfo, &m_instance), "instance"))
      return false;

    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!check(xrGetSystem(m_instance, &systemInfo, &m_system), "system"))
      return false;

    uint32_t viewCount = 0;
    if (!check(xrEnumerateViewConfigurationViews(m_instance, m_system,
                                                 XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0,
                                                 &viewCount, nullptr),
               "view count") ||
        viewCount < 2)
      return false;

    std::vector<XrViewConfigurationView> views(
      viewCount, XrViewConfigurationView{XR_TYPE_VIEW_CONFIGURATION_VIEW});
    if (!check(xrEnumerateViewConfigurationViews(m_instance, m_system,
                                                 XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                 viewCount, &viewCount, views.data()),
               "views"))
      return false;

    m_eyeWidth = views[0].recommendedImageRectWidth;
    m_eyeHeight = views[0].recommendedImageRectHeight;
    note("pmp-xr-host: per eye %ux%u", m_eyeWidth, m_eyeHeight);
    return true;
  }

  bool Presenter::createDevice()
  {
    PFN_xrGetVulkanGraphicsRequirements2KHR getRequirements = nullptr;
    PFN_xrCreateVulkanInstanceKHR createVulkanInstance = nullptr;
    PFN_xrGetVulkanGraphicsDevice2KHR getGraphicsDevice = nullptr;
    PFN_xrCreateVulkanDeviceKHR createVulkanDevice = nullptr;

    if (XR_FAILED(xrGetInstanceProcAddr(m_instance, "xrGetVulkanGraphicsRequirements2KHR",
                                        reinterpret_cast<PFN_xrVoidFunction*>(&getRequirements))) ||
        XR_FAILED(xrGetInstanceProcAddr(m_instance, "xrCreateVulkanInstanceKHR",
                                        reinterpret_cast<PFN_xrVoidFunction*>(&createVulkanInstance))) ||
        XR_FAILED(xrGetInstanceProcAddr(m_instance, "xrGetVulkanGraphicsDevice2KHR",
                                        reinterpret_cast<PFN_xrVoidFunction*>(&getGraphicsDevice))) ||
        XR_FAILED(xrGetInstanceProcAddr(m_instance, "xrCreateVulkanDeviceKHR",
                                        reinterpret_cast<PFN_xrVoidFunction*>(&createVulkanDevice))))
    {
      note("pmp-xr-host: vulkan-enable2 entry points missing");
      return false;
    }

    XrGraphicsRequirementsVulkan2KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
    if (!check(getRequirements(m_instance, m_system, &requirements), "graphics requirements"))
      return false;

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "pmp-xr-host";
    appInfo.applicationVersion = 1;
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &appInfo;

    XrVulkanInstanceCreateInfoKHR xrInstanceInfo{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
    xrInstanceInfo.systemId = m_system;
    xrInstanceInfo.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
    xrInstanceInfo.vulkanCreateInfo = &instanceInfo;

    VkResult vkResult = VK_SUCCESS;
    if (!check(createVulkanInstance(m_instance, &xrInstanceInfo, &m_vkInstance, &vkResult),
               "vulkan instance") ||
        !checkVk(vkResult, "vulkan instance"))
      return false;

    XrVulkanGraphicsDeviceGetInfoKHR deviceGetInfo{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
    deviceGetInfo.systemId = m_system;
    deviceGetInfo.vulkanInstance = m_vkInstance;
    if (!check(getGraphicsDevice(m_instance, &deviceGetInfo, &m_physical), "graphics device"))
      return false;

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

    const char* deviceExtensions[] = {VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
                                      VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
                                      VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
                                      VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME,
                                      VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME};

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
    deviceInfo.enabledExtensionCount = sizeof(deviceExtensions) / sizeof(deviceExtensions[0]);
    deviceInfo.ppEnabledExtensionNames = deviceExtensions;

    XrVulkanDeviceCreateInfoKHR xrDeviceInfo{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
    xrDeviceInfo.systemId = m_system;
    xrDeviceInfo.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
    xrDeviceInfo.vulkanPhysicalDevice = m_physical;
    xrDeviceInfo.vulkanCreateInfo = &deviceInfo;

    if (!check(createVulkanDevice(m_instance, &xrDeviceInfo, &m_device, &vkResult),
               "vulkan device") ||
        !checkVk(vkResult, "vulkan device"))
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

  bool Presenter::createSession()
  {
    XrGraphicsBindingVulkan2KHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
    binding.instance = m_vkInstance;
    binding.physicalDevice = m_physical;
    binding.device = m_device;
    binding.queueFamilyIndex = m_queueFamily;
    binding.queueIndex = 0;

    XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
    sessionInfo.next = &binding;
    sessionInfo.systemId = m_system;
    if (!check(xrCreateSession(m_instance, &sessionInfo, &m_session), "session"))
      return false;

    XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;
    if (!check(xrCreateReferenceSpace(m_session, &spaceInfo, &m_localSpace), "local space"))
      return false;

    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    if (!check(xrCreateReferenceSpace(m_session, &spaceInfo, &m_viewSpace), "view space"))
      return false;

    m_screenPose.orientation.w = 1.0f;
    m_screenPose.position.z = -kScreenDistance;
    return true;
  }

  bool Presenter::createSwapchains()
  {
    uint32_t formatCount = 0;
    xrEnumerateSwapchainFormats(m_session, 0, &formatCount, nullptr);
    std::vector<int64_t> formats(formatCount);
    if (formatCount > 0)
      xrEnumerateSwapchainFormats(m_session, formatCount, &formatCount, formats.data());

    const int64_t preferred[] = {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB,
                                 VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM};
    m_swapchainFormat = formats.empty() ? 0 : formats[0];
    for (int64_t want : preferred)
    {
      bool found = false;
      for (int64_t have : formats)
        if (have == want)
          found = true;
      if (found)
      {
        m_swapchainFormat = want;
        break;
      }
    }

    m_sourceFormat = (m_swapchainFormat == VK_FORMAT_R8G8B8A8_SRGB ||
                      m_swapchainFormat == VK_FORMAT_B8G8R8A8_SRGB)
                       ? VK_FORMAT_R8G8B8A8_SRGB
                       : VK_FORMAT_R8G8B8A8_UNORM;

    for (int eye = 0; eye < 2; ++eye)
    {
      XrSwapchainCreateInfo swapchainInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
      swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                                 XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
                                 XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
      swapchainInfo.format = m_swapchainFormat;
      swapchainInfo.sampleCount = 1;
      swapchainInfo.width = m_eyeWidth;
      swapchainInfo.height = m_eyeHeight;
      swapchainInfo.faceCount = 1;
      swapchainInfo.arraySize = 1;
      swapchainInfo.mipCount = 1;

      if (!check(xrCreateSwapchain(m_session, &swapchainInfo, &m_eyes[eye].swapchain),
                 "swapchain"))
        return false;

      uint32_t imageCount = 0;
      xrEnumerateSwapchainImages(m_eyes[eye].swapchain, 0, &imageCount, nullptr);
      std::vector<XrSwapchainImageVulkanKHR> images(
        imageCount, XrSwapchainImageVulkanKHR{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
      if (!check(xrEnumerateSwapchainImages(
                   m_eyes[eye].swapchain, imageCount, &imageCount,
                   reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),
                 "swapchain images"))
        return false;

      for (const XrSwapchainImageVulkanKHR& image : images)
        m_eyes[eye].images.push_back(image.image);
    }

    note("pmp-xr-host: swapchains ready, format %lld", static_cast<long long>(m_swapchainFormat));
    return true;
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
      externalInfo.pNext = &modifierInfo;
      externalInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

      VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
      imageInfo.pNext = &externalInfo;
      imageInfo.imageType = VK_IMAGE_TYPE_2D;
      imageInfo.format = m_sourceFormat;
      imageInfo.extent = {message.width, message.height, 1};
      imageInfo.mipLevels = 1;
      imageInfo.arrayLayers = 1;
      imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
      imageInfo.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
      imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
      imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

      if (!checkVk(vkCreateImage(m_device, &imageInfo, nullptr, &source.image), "import image"))
        return false;

      VkMemoryRequirements requirements{};
      vkGetImageMemoryRequirements(m_device, source.image, &requirements);

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
              m_screenSize = {kScreenWidth, kScreenWidth / message.eyeAspect};
          }
          break;

        case XrProtocol::Recentre:
          m_screenPlaced = false;
          break;

        case XrProtocol::Stop:
          note("pmp-xr-host: the app asked to stop");
          return false;

        default:
          break;
      }
    }
  }

  void Presenter::placeScreen(XrTime time)
  {
    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
    if (XR_FAILED(xrLocateSpace(m_viewSpace, m_localSpace, time, &location)))
      return;
    if (!(location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) ||
        !(location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT))
      return;

    const XrQuaternionf& q = location.pose.orientation;
    const float yaw = std::atan2(2.0f * (q.w * q.y + q.x * q.z),
                                 1.0f - 2.0f * (q.y * q.y + q.x * q.x));

    m_screenPose.orientation = {0.0f, std::sin(yaw * 0.5f), 0.0f, std::cos(yaw * 0.5f)};
    m_screenPose.position = {location.pose.position.x - std::sin(yaw) * kScreenDistance,
                             location.pose.position.y,
                             location.pose.position.z - std::cos(yaw) * kScreenDistance};
    m_screenPlaced = true;
    note("pmp-xr-host: screen placed at %.2f %.2f %.2f", m_screenPose.position.x,
         m_screenPose.position.y, m_screenPose.position.z);
  }

  bool Presenter::blit(int eye, uint32_t imageIndex)
  {
    const Source& source = m_sources[m_current];

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
    target.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    target.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    target.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    target.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    target.image = m_eyes[eye].images[imageIndex];
    target.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    target.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    VkImageMemoryBarrier before[2] = {acquire, target};
    vkCmdPipelineBarrier(m_command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, before);

    VkImageBlit region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.srcOffsets[0] = {x0, y0, 0};
    region.srcOffsets[1] = {x1, y1, 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstOffsets[0] = {0, 0, 0};
    region.dstOffsets[1] = {static_cast<int32_t>(m_eyeWidth), static_cast<int32_t>(m_eyeHeight),
                            1};

    vkCmdBlitImage(m_command, source.image, VK_IMAGE_LAYOUT_GENERAL,
                   m_eyes[eye].images[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                   &region, VK_FILTER_LINEAR);

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
    present.image = m_eyes[eye].images[imageIndex];
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
    return true;
  }

  void Presenter::frame()
  {
    XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState frameState{XR_TYPE_FRAME_STATE};
    if (XR_FAILED(xrWaitFrame(m_session, &waitInfo, &frameState)))
      return;

    XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    if (XR_FAILED(xrBeginFrame(m_session, &beginInfo)))
      return;

    std::vector<XrCompositionLayerBaseHeader*> layers;
    XrCompositionLayerQuad quads[2];

    const bool draw = frameState.shouldRender && m_current >= 0 && m_sources[m_current].ready;

    if (draw)
    {
      if (!m_screenPlaced)
        placeScreen(frameState.predictedDisplayTime);

      for (int eye = 0; eye < 2; ++eye)
      {
        uint32_t imageIndex = 0;
        XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        if (XR_FAILED(xrAcquireSwapchainImage(m_eyes[eye].swapchain, &acquireInfo, &imageIndex)))
          break;

        XrSwapchainImageWaitInfo waitImage{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        waitImage.timeout = XR_INFINITE_DURATION;
        if (XR_FAILED(xrWaitSwapchainImage(m_eyes[eye].swapchain, &waitImage)))
          break;

        blit(eye, imageIndex);

        XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        xrReleaseSwapchainImage(m_eyes[eye].swapchain, &releaseInfo);

        quads[eye] = XrCompositionLayerQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
        quads[eye].layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        quads[eye].space = m_localSpace;
        quads[eye].eyeVisibility = eye == 0 ? XR_EYE_VISIBILITY_LEFT : XR_EYE_VISIBILITY_RIGHT;
        quads[eye].subImage.swapchain = m_eyes[eye].swapchain;
        quads[eye].subImage.imageRect = {
          {0, 0}, {static_cast<int32_t>(m_eyeWidth), static_cast<int32_t>(m_eyeHeight)}};
        quads[eye].subImage.imageArrayIndex = 0;
        quads[eye].pose = m_screenPose;
        quads[eye].size = m_screenSize;

        layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&quads[eye]));
      }

      if (layers.size() == 2 && m_presented == 0)
      {
        note("pmp-xr-host: presenting");
        m_presented = 1;
      }
    }

    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = static_cast<uint32_t>(layers.size());
    endInfo.layers = layers.empty() ? nullptr : layers.data();
    xrEndFrame(m_session, &endInfo);
  }

  void Presenter::pollEvents(bool& exit)
  {
    while (true)
    {
      XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
      const XrResult result = xrPollEvent(m_instance, &event);
      if (result != XR_SUCCESS)
        return;

      if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
      {
        const auto* changed = reinterpret_cast<XrEventDataSessionStateChanged*>(&event);
        m_state = changed->state;

        if (m_state == XR_SESSION_STATE_READY)
        {
          XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
          beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
          if (check(xrBeginSession(m_session, &beginInfo), "begin session"))
          {
            m_running = true;
            note("pmp-xr-host: session running");
          }
        }
        else if (m_state == XR_SESSION_STATE_STOPPING)
        {
          m_running = false;
          xrEndSession(m_session);
        }
        else if (m_state == XR_SESSION_STATE_EXITING ||
                 m_state == XR_SESSION_STATE_LOSS_PENDING)
        {
          exit = true;
        }
      }
      else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
      {
        exit = true;
      }
    }
  }

  bool Presenter::setup()
  {
    return createInstance() && createDevice() && createSession() && createSwapchains();
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

      if (!m_running)
      {
        sleepMs(10);
        continue;
      }

      frame();
    }

    note("pmp-xr-host: presenter stopping");
    return 0;
  }

  void Presenter::teardown()
  {
    if (m_device != VK_NULL_HANDLE)
      vkDeviceWaitIdle(m_device);

    for (int eye = 0; eye < 2; ++eye)
    {
      if (m_eyes[eye].swapchain != XR_NULL_HANDLE)
        xrDestroySwapchain(m_eyes[eye].swapchain);
    }

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

    if (m_localSpace != XR_NULL_HANDLE)
      xrDestroySpace(m_localSpace);
    if (m_viewSpace != XR_NULL_HANDLE)
      xrDestroySpace(m_viewSpace);
    if (m_session != XR_NULL_HANDLE)
      xrDestroySession(m_session);

    if (m_fence != VK_NULL_HANDLE)
      vkDestroyFence(m_device, m_fence, nullptr);
    if (m_pool != VK_NULL_HANDLE)
      vkDestroyCommandPool(m_device, m_pool, nullptr);
    if (m_device != VK_NULL_HANDLE)
      vkDestroyDevice(m_device, nullptr);
    if (m_vkInstance != VK_NULL_HANDLE)
      vkDestroyInstance(m_vkInstance, nullptr);
    if (m_instance != XR_NULL_HANDLE)
      xrDestroyInstance(m_instance);

    m_device = VK_NULL_HANDLE;
    m_vkInstance = VK_NULL_HANDLE;
    m_instance = XR_NULL_HANDLE;
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
