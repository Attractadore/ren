#include "Vulkan/VulkanDevice.hpp"
#include "Support/Array.hpp"
#include "Support/Views.hpp"
#include "Vulkan/VulkanBuffer.hpp"
#include "Vulkan/VulkanCommandAllocator.hpp"
#include "Vulkan/VulkanDeleteQueue.inl"
#include "Vulkan/VulkanErrors.hpp"
#include "Vulkan/VulkanFormats.hpp"
#include "Vulkan/VulkanRenderGraph.hpp"
#include "Vulkan/VulkanSwapchain.hpp"
#include "Vulkan/VulkanTexture.hpp"

constexpr bool operator==(const VkImageViewCreateInfo &lhs,
                          const VkImageViewCreateInfo &rhs) {
  return lhs.flags == rhs.flags and lhs.viewType == rhs.viewType and
         lhs.format == rhs.format and
         lhs.subresourceRange.aspectMask == rhs.subresourceRange.aspectMask and
         lhs.subresourceRange.baseMipLevel ==
             rhs.subresourceRange.baseMipLevel and
         lhs.subresourceRange.levelCount == rhs.subresourceRange.levelCount and
         lhs.subresourceRange.baseArrayLayer ==
             rhs.subresourceRange.baseArrayLayer and
         lhs.subresourceRange.layerCount == rhs.subresourceRange.layerCount;
}

namespace ren {
std::span<const char *const> VulkanDevice::getRequiredLayers() {
  static constexpr auto layers = makeArray<const char *>(
#if REN_VULKAN_VALIDATION
      "VK_LAYER_KHRONOS_validation"
#endif
  );
  return layers;
}

std::span<const char *const> VulkanDevice::getRequiredExtensions() {
  static constexpr auto extensions = makeArray<const char *>();
  return extensions;
}

namespace {
int findQueueFamilyWithCapabilities(VulkanDevice *device, VkQueueFlags caps) {
  unsigned qcnt = 0;
  device->GetPhysicalDeviceQueueFamilyProperties(&qcnt, nullptr);
  SmallVector<VkQueueFamilyProperties, 4> queues(qcnt);
  device->GetPhysicalDeviceQueueFamilyProperties(&qcnt, queues.data());
  for (unsigned i = 0; i < qcnt; ++i) {
    constexpr auto filter =
        VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    if ((queues[i].queueFlags & filter) == caps) {
      return i;
    }
  }
  return -1;
}

int findGraphicsQueueFamily(VulkanDevice *device) {
  return findQueueFamilyWithCapabilities(device, VK_QUEUE_GRAPHICS_BIT |
                                                     VK_QUEUE_COMPUTE_BIT |
                                                     VK_QUEUE_TRANSFER_BIT);
}
} // namespace

VulkanDevice::VulkanDevice(PFN_vkGetInstanceProcAddr proc, VkInstance instance,
                           VkPhysicalDevice m_adapter)
    : m_instance(instance), m_adapter(m_adapter) {
  loadInstanceFunctions(proc, m_instance, &m_vk);

  m_graphics_queue_family = findGraphicsQueueFamily(this);

  float queue_priority = 1.0f;
  VkDeviceQueueCreateInfo queue_create_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = m_graphics_queue_family,
      .queueCount = 1,
      .pQueuePriorities = &queue_priority,
  };

  VkPhysicalDeviceVulkan12Features vulkan12_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
      .scalarBlockLayout = true,
      .timelineSemaphore = true,
  };

  VkPhysicalDeviceVulkan13Features vulkan13_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .pNext = &vulkan12_features,
      .synchronization2 = true,
      .dynamicRendering = true,
  };

  std::array extensions = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME,
  };

  VkDeviceCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &vulkan13_features,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queue_create_info,
      .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
      .ppEnabledExtensionNames = extensions.data(),
  };

  throwIfFailed(CreateDevice(&create_info, &m_device),
                "Vulkan: Failed to create device");

  loadDeviceFunctions(m_vk.GetDeviceProcAddr, m_device, &m_vk);

  GetDeviceQueue(m_graphics_queue_family, 0, &m_graphics_queue);
  m_graphics_queue_semaphore = createTimelineSemaphore();

  VmaVulkanFunctions vma_vulkan_functions = {
      .vkGetInstanceProcAddr = m_vk.GetInstanceProcAddr,
      .vkGetDeviceProcAddr = m_vk.GetDeviceProcAddr,
  };

  VmaAllocatorCreateInfo allocator_info = {
      .physicalDevice = m_adapter,
      .device = m_device,
      .pAllocationCallbacks = getAllocator(),
      .pVulkanFunctions = &vma_vulkan_functions,
      .instance = m_instance,
      .vulkanApiVersion = getRequiredAPIVersion(),
  };

  throwIfFailed(vmaCreateAllocator(&allocator_info, &m_allocator),
                "VMA: Failed to create allocator");

  m_cmd_alloc = VulkanCommandAllocator(*this);

  new (&m_pipeline_compiler) VulkanPipelineCompiler(*this);
}

VulkanDevice::~VulkanDevice() {
  m_pipeline_compiler.~VulkanPipelineCompiler();
  m_cmd_alloc = VulkanCommandAllocator();
  waitForIdle();
  m_delete_queue.flush(*this);
  DestroySemaphore(m_graphics_queue_semaphore);
  vmaDestroyAllocator(m_allocator);
  DestroyDevice();
}

void VulkanDevice::begin_frame() {
  m_frame_index = (m_frame_index + 1) % m_frame_end_times.size();
  waitForGraphicsQueue(m_frame_end_times[m_frame_index].graphics_queue_time);
  m_delete_queue.begin_frame(*this);
  m_cmd_alloc.begin_frame();
}

void VulkanDevice::end_frame() {
  m_cmd_alloc.end_frame();
  m_delete_queue.end_frame(*this);
  m_frame_end_times[m_frame_index].graphics_queue_time = getGraphicsQueueTime();
}

Buffer VulkanDevice::create_buffer(const BufferDesc &in_desc) {
  BufferDesc desc = in_desc;
  desc.offset = 0;

  VkBufferCreateInfo buffer_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = desc.size,
      .usage = getVkBufferUsageFlags(desc.usage),
  };

  VmaAllocationCreateInfo alloc_info = {
      .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
      .usage = VMA_MEMORY_USAGE_AUTO,
  };

  switch (desc.location) {
    using enum BufferLocation;
  case Device:
    break;
  case Host:
    alloc_info.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    break;
  case HostCached:
    alloc_info.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    break;
  }

  VkBuffer buffer;
  VmaAllocation allocation;
  VmaAllocationInfo map_info;
  throwIfFailed(vmaCreateBuffer(m_allocator, &buffer_info, &alloc_info, &buffer,
                                &allocation, &map_info),
                "VMA: Failed to create buffer");
  desc.ptr = map_info.pMappedData;

  return {.desc = desc,
          .handle = AnyRef(buffer, [this, allocation](VkBuffer buffer) {
            push_to_delete_queue(buffer);
            push_to_delete_queue(allocation);
          })};
}

Texture VulkanDevice::createTexture(const TextureDesc &desc) {
  VkImageCreateInfo image_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = getVkImageType(desc.type),
      .format = getVkFormat(desc.format),
      .extent = {desc.width, desc.height, desc.depth},
      .mipLevels = desc.levels,
      .arrayLayers = desc.layers,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = getVkImageUsageFlags(desc.usage),
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };

  VmaAllocationCreateInfo alloc_info = {.usage = VMA_MEMORY_USAGE_AUTO};

  VkImage image;
  VmaAllocation allocation;
  throwIfFailed(vmaCreateImage(m_allocator, &image_info, &alloc_info, &image,
                               &allocation, nullptr),
                "VMA: Failed to create image");

  return {.desc = desc,
          .handle = AnyRef(image, [this, allocation](VkImage image) {
            push_to_delete_queue(VulkanImageViews{image});
            push_to_delete_queue(image);
            push_to_delete_queue(allocation);
          })};
}

void VulkanDevice::destroyImageViews(VkImage image) {
  for (auto &&[_, view] : m_image_views[image]) {
    DestroyImageView(view);
  }
  m_image_views.erase(image);
}

VkImageView VulkanDevice::getVkImageView(const RenderTargetView &rtv) {
  auto image = getVkImage(rtv.texture);
  VkImageViewCreateInfo view_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = image,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = getVkFormat(getRTVFormat(rtv)),
      .subresourceRange = {
          .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
          .baseMipLevel = rtv.desc.level,
          .levelCount = 1,
          .baseArrayLayer = rtv.desc.layer,
          .layerCount = 1,
      }};
  return getVkImageViewImpl(image, view_info);
}

VkImageView VulkanDevice::getVkImageView(const DepthStencilView &dsv) {
  auto image = getVkImage(dsv.texture);
  auto format = getDSVFormat(dsv);
  VkImageViewCreateInfo view_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = image,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = getVkFormat(format),
      .subresourceRange = {
          .aspectMask = getFormatAspectFlags(format),
          .baseMipLevel = dsv.desc.level,
          .levelCount = 1,
          .baseArrayLayer = dsv.desc.layer,
          .layerCount = 1,
      }};
  return getVkImageViewImpl(image, view_info);
}

VkImageView
VulkanDevice::getVkImageViewImpl(VkImage image,
                                 const VkImageViewCreateInfo &view_info) {
  if (!image) {
    return VK_NULL_HANDLE;
  }
  auto [it, inserted] = m_image_views[image].insert(view_info, VK_NULL_HANDLE);
  auto &view = std::get<1>(*it);
  if (inserted) {
    throwIfFailed(CreateImageView(&view_info, &view),
                  "Vulkan: Failed to create image view");
  }
  return view;
}

VkSemaphore VulkanDevice::createBinarySemaphore() {
  VkSemaphoreCreateInfo semaphore_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
  };
  VkSemaphore semaphore;
  throwIfFailed(CreateSemaphore(&semaphore_info, &semaphore),
                "Vulkan: Failed to create binary semaphore");
  return semaphore;
}

VkSemaphore VulkanDevice::createTimelineSemaphore(uint64_t initial_value) {
  VkSemaphoreTypeCreateInfo semaphore_type_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
      .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
      .initialValue = initial_value,
  };
  VkSemaphoreCreateInfo semaphore_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = &semaphore_type_info,
  };
  VkSemaphore semaphore;
  throwIfFailed(CreateSemaphore(&semaphore_info, &semaphore),
                "Vulkan: Failed to create timeline semaphore");
  return semaphore;
}

SemaphoreWaitResult
VulkanDevice::waitForSemaphore(VkSemaphore sem, uint64_t value,
                               std::chrono::nanoseconds timeout) const {
  VkSemaphoreWaitInfo wait_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
      .semaphoreCount = 1,
      .pSemaphores = &sem,
      .pValues = &value,
  };
  auto r = WaitSemaphores(&wait_info, timeout.count());
  switch (r) {
    using enum SemaphoreWaitResult;
  case VK_SUCCESS:
    return Ready;
  case VK_TIMEOUT:
    return Timeout;
  default:
    throw std::runtime_error{"Vulkan: Failed to wait for semaphore"};
  };
}

std::unique_ptr<RenderGraph::Builder> VulkanDevice::createRenderGraphBuilder() {
  return std::make_unique<VulkanRenderGraph::Builder>(*this);
}

SyncObject VulkanDevice::createSyncObject(const SyncDesc &desc) {
  assert(desc.type == SyncType::Semaphore);
  return {.desc = desc,
          .handle =
              AnyRef(createBinarySemaphore(), [this](VkSemaphore semaphore) {
                push_to_delete_queue(semaphore);
              })};
}

std::unique_ptr<VulkanSwapchain>
VulkanDevice::createSwapchain(VkSurfaceKHR surface) {
  return std::make_unique<VulkanSwapchain>(this, surface);
}

void VulkanDevice::queueSubmitAndSignal(VkQueue queue,
                                        std::span<const VulkanSubmit> submits,
                                        VkSemaphore semaphore, uint64_t value) {
  auto submit_infos =
      submits | map([](const VulkanSubmit &submit) {
        return VkSubmitInfo2{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .waitSemaphoreInfoCount = uint32_t(submit.wait_semaphores.size()),
            .pWaitSemaphoreInfos = submit.wait_semaphores.data(),
            .commandBufferInfoCount = uint32_t(submit.command_buffers.size()),
            .pCommandBufferInfos = submit.command_buffers.data(),
            .signalSemaphoreInfoCount =
                uint32_t(submit.signal_semaphores.size()),
            .pSignalSemaphoreInfos = submit.signal_semaphores.data(),
        };
      }) |
      ranges::to<SmallVector<VkSubmitInfo2, 8>>;

  auto final_signal_semaphores =
      concat(submits.back().signal_semaphores,
             once(VkSemaphoreSubmitInfo{
                 .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                 .semaphore = semaphore,
                 .value = value,
             })) |
      ranges::to<SmallVector<VkSemaphoreSubmitInfo, 8>>;

  auto &final_submit_info = submit_infos.back();
  final_submit_info.signalSemaphoreInfoCount = final_signal_semaphores.size();
  final_submit_info.pSignalSemaphoreInfos = final_signal_semaphores.data();

  throwIfFailed(QueueSubmit2(queue, submit_infos.size(), submit_infos.data(),
                             VK_NULL_HANDLE),
                "Vulkan: Failed to submit work to queue");
}
} // namespace ren
