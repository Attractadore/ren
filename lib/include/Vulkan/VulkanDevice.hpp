#pragma once
#include "Config.hpp"
#include "Device.hpp"
#include "Support/Errors.hpp"
#include "Support/LinearMap.hpp"
#include "Support/Span.hpp"
#include "VMA.h"
#include "VulkanCommandAllocator.hpp"
#include "VulkanDeleteQueue.hpp"
#include "VulkanDispatchTable.hpp"
#include "VulkanErrors.hpp"

#include <cassert>
#include <chrono>

namespace ren {
class VulkanSwapchain;

enum class SemaphoreWaitResult {
  Ready,
  Timeout,
};

struct VulkanSubmit {
  std::span<const VkSemaphoreSubmitInfo> wait_semaphores;
  std::span<const VkCommandBufferSubmitInfo> command_buffers;
  std::span<const VkSemaphoreSubmitInfo> signal_semaphores;
};

struct VulkanDeviceTime {
  uint64_t graphics_queue_time;
};

class VulkanDevice final : public Device,
                           public InstanceFunctionsMixin<VulkanDevice>,
                           public PhysicalDeviceFunctionsMixin<VulkanDevice>,
                           public DeviceFunctionsMixin<VulkanDevice> {
  VkInstance m_instance;
  VkPhysicalDevice m_adapter;
  VkDevice m_device;
  VmaAllocator m_allocator;
  VulkanDispatchTable m_vk = {};

  unsigned m_graphics_queue_family = -1;
  VkQueue m_graphics_queue;
  VkSemaphore m_graphics_queue_semaphore;
  uint64_t m_graphics_queue_time = 0;

  unsigned m_frame_index = 0;
  std::array<VulkanDeviceTime, c_pipeline_depth> m_frame_end_times = {};

  HashMap<VkImage, SmallLinearMap<VkImageViewCreateInfo, VkImageView, 3>>
      m_image_views;

  VulkanDeleteQueue m_delete_queue;

private:
  auto create_buffer_handle(const BufferDesc &desc)
      -> std::pair<SharedHandle<VkBuffer>, void *> override;

  auto create_graphics_pipeline_handle(const GraphicsPipelineConfig &desc)
      -> SharedHandle<VkPipeline> override;

private:
  VkImageView getVkImageViewImpl(VkImage image,
                                 const VkImageViewCreateInfo &view_info);

  void queueSubmitAndSignal(VkQueue queue,
                            std::span<const VulkanSubmit> submits,
                            VkSemaphore semaphore, uint64_t value);

public:
  VulkanDevice(PFN_vkGetInstanceProcAddr proc, VkInstance instance,
               VkPhysicalDevice adapter);
  VulkanDevice(const VulkanDevice &) = delete;
  VulkanDevice(VulkanDevice &&);
  VulkanDevice &operator=(const VulkanDevice &) = delete;
  VulkanDevice &operator=(VulkanDevice &&);
  ~VulkanDevice();

  void begin_frame() override;
  void end_frame() override;

  auto supports_feature(DeviceFeature feature) const -> bool override;

  static uint32_t getRequiredAPIVersion() { return VK_API_VERSION_1_3; }
  static std::span<const char *const> getRequiredLayers();
  static std::span<const char *const> getRequiredExtensions();

  const VulkanDispatchTable &getDispatchTable() const { return m_vk; }

  VkInstance getInstance() const { return m_instance; }
  VkPhysicalDevice getPhysicalDevice() const { return m_adapter; }
  VkDevice getDevice() const { return m_device; }
  VmaAllocator getVMAAllocator() const { return m_allocator; }
  const VkAllocationCallbacks *getAllocator() const { return nullptr; }

  auto create_command_allocator(QueueType queue_type)
      -> std::unique_ptr<CommandAllocator> override;

  auto create_descriptor_pool(const DescriptorPoolDesc &desc)
      -> DescriptorPool override;

  void reset_descriptor_pool(const DescriptorPoolRef &pool) override;

  auto create_descriptor_set_layout(const DescriptorSetLayoutDesc &desc)
      -> DescriptorSetLayout override;

  auto allocate_descriptor_sets(const DescriptorPoolRef &pool,
                                std::span<const DescriptorSetLayoutRef> layouts,
                                std::span<VkDescriptorSet> sets)
      -> bool override;

  void
  write_descriptor_sets(std::span<const VkWriteDescriptorSet> configs) override;

  auto get_shader_blob_suffix() const -> std::string_view override {
    return ".spv";
  }

  auto get_shader_reflection_suffix() const -> std::string_view override {
    return ".spv";
  }

  auto create_reflection_module(std::span<const std::byte> data)
      -> std::unique_ptr<ReflectionModule> override;

  auto create_pipeline_signature(const PipelineSignatureDesc &desc)
      -> PipelineSignature override;

  auto get_buffer_device_address(const BufferRef &buffer) const
      -> uint64_t override;

  Texture createTexture(const TextureDesc &desc) override;
  void destroyImageViews(VkImage image);

  VkImageView getVkImageView(const RenderTargetView &rtv);
  VkImageView getVkImageView(const DepthStencilView &dsv);

  auto createBinarySemaphore() -> Semaphore;
  auto createTimelineSemaphore(uint64_t initial_value = 0) -> VkSemaphore;
  SemaphoreWaitResult waitForSemaphore(VkSemaphore sem, uint64_t value,
                                       std::chrono::nanoseconds timeout) const;
  void waitForSemaphore(VkSemaphore sem, uint64_t value) const {
    auto r = waitForSemaphore(sem, value, std::chrono::nanoseconds::max());
    assert(r == SemaphoreWaitResult::Ready);
  }
  uint64_t getSemaphoreValue(VkSemaphore semaphore) const {
    uint64_t value;
    throwIfFailed(GetSemaphoreCounterValue(semaphore, &value),
                  "Vulkan: Failed to get semaphore value");
    return value;
  }

  VkQueue getGraphicsQueue() const { return m_graphics_queue; }
  unsigned getGraphicsQueueFamily() const { return m_graphics_queue_family; }
  VkSemaphore getGraphicsQueueSemaphore() const {
    return m_graphics_queue_semaphore;
  }
  uint64_t getGraphicsQueueTime() const { return m_graphics_queue_time; }
  uint64_t getGraphicsQueueCompletedTime() const {
    return getSemaphoreValue(getGraphicsQueueSemaphore());
  }

  void graphicsQueueSubmit(std::span<const VulkanSubmit> submits) {
    queueSubmitAndSignal(getGraphicsQueue(), submits,
                         getGraphicsQueueSemaphore(), ++m_graphics_queue_time);
  }
  void waitForGraphicsQueue(uint64_t time) const {
    waitForSemaphore(getGraphicsQueueSemaphore(), time);
  }
  void waitForIdleGraphicsQueue() {
    throwIfFailed(QueueWaitIdle(getGraphicsQueue()),
                  "Vulkan: Failed to wait for idle graphics queue");
  }

  VkResult queuePresent(const VkPresentInfoKHR &present_info) {
    auto queue = getGraphicsQueue();
    auto r = QueuePresentKHR(queue, &present_info);
    switch (r) {
    case VK_SUCCESS:
    case VK_SUBOPTIMAL_KHR:
    case VK_ERROR_OUT_OF_DATE_KHR:
    case VK_ERROR_SURFACE_LOST_KHR:
    case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT: {
#if 1
      VkSemaphoreSubmitInfo semaphore_info = {
          .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
          .semaphore = getGraphicsQueueSemaphore(),
          .value = ++m_graphics_queue_time,
      };
      VkSubmitInfo2 submit_info = {
          .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
          .signalSemaphoreInfoCount = 1,
          .pSignalSemaphoreInfos = &semaphore_info,
      };
      throwIfFailed(QueueSubmit2(queue, 1, &submit_info, VK_NULL_HANDLE),
                    "Vulkan: Failed to submit semaphore signal operation");
#else
      // NOTE: bad stuff (like a dead lock) will happen if someones tries to
      // wait for this value before signaling the graphics queue with a higher
      // value.
      ++m_graphics_queue_time;
#endif
    }
    }
    return r;
  }

  void waitForIdle() const {
    throwIfFailed(DeviceWaitIdle(), "Vulkan: Failed to wait for idle device");
  }

  std::unique_ptr<RenderGraph::Builder> createRenderGraphBuilder() override;

  std::unique_ptr<VulkanSwapchain> createSwapchain(VkSurfaceKHR surface);

  template <typename T> void push_to_delete_queue(T value) {
    m_delete_queue.push(std::move(value));
  }

  void push_to_delete_queue(QueueCustomDeleter<Device> deleter) override {
    m_delete_queue.push(VulkanQueueCustomDeleter(std::move(deleter)));
  }
};
} // namespace ren
