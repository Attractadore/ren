#pragma once
#include "DeleteQueue.hpp"
#include "Descriptors.hpp"
#include "Pipeline.hpp"
#include "Reflection.hpp"
#include "RenderGraph.hpp"
#include "Support/LinearMap.hpp"
#include "Vulkan/VMA.h"
#include "VulkanDispatchTable.hpp"

#include <chrono>

namespace ren {

enum class QueueType {
  Graphics,
  Compute,
  Transfer,
};

enum class SemaphoreWaitResult {
  Ready,
  Timeout,
};

struct Submit {
  std::span<const VkSemaphoreSubmitInfo> wait_semaphores;
  std::span<const VkCommandBufferSubmitInfo> command_buffers;
  std::span<const VkSemaphoreSubmitInfo> signal_semaphores;
};

struct DeviceTime {
  uint64_t graphics_queue_time;
};

class Device : public InstanceFunctionsMixin<Device>,
               public PhysicalDeviceFunctionsMixin<Device>,
               public DeviceFunctionsMixin<Device> {
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
  std::array<DeviceTime, c_pipeline_depth> m_frame_end_times = {};

  HashMap<VkImage, SmallLinearMap<VkImageViewCreateInfo, VkImageView, 3>>
      m_image_views;

  DeleteQueue m_delete_queue;

private:
  [[nodiscard]] auto getVkImageView(const VkImageViewCreateInfo &view_info)
      -> VkImageView;

  void queueSubmitAndSignal(VkQueue queue, std::span<const Submit> submits,
                            VkSemaphore semaphore, uint64_t value);

public:
  Device(PFN_vkGetInstanceProcAddr proc, VkInstance instance,
         VkPhysicalDevice adapter);
  Device(const Device &) = delete;
  Device(Device &&);
  Device &operator=(const Device &) = delete;
  Device &operator=(Device &&);
  ~Device();

  void begin_frame();
  void end_frame();

  [[nodiscard]] static auto getRequiredAPIVersion() -> uint32_t {
    return VK_API_VERSION_1_3;
  }

  [[nodiscard]] static auto getRequiredLayers() -> std::span<const char *const>;
  [[nodiscard]] static auto getRequiredExtensions()
      -> std::span<const char *const>;

  [[nodiscard]] auto getDispatchTable() const -> const VulkanDispatchTable & {
    return m_vk;
  }

  [[nodiscard]] auto getInstance() const -> VkInstance { return m_instance; }

  [[nodiscard]] auto getPhysicalDevice() const -> VkPhysicalDevice {
    return m_adapter;
  }

  [[nodiscard]] auto getDevice() const -> VkDevice { return m_device; }

  [[nodiscard]] auto getVMAAllocator() const -> VmaAllocator {
    return m_allocator;
  }
  [[nodiscard]] auto getAllocator() const -> const VkAllocationCallbacks * {
    return nullptr;
  }

  template <typename T> void push_to_delete_queue(T value) {
    m_delete_queue.push(std::move(value));
  }

  [[nodiscard]] auto create_descriptor_pool(const DescriptorPoolDesc &desc)
      -> DescriptorPool;

  void reset_descriptor_pool(const DescriptorPoolRef &pool);

  [[nodiscard]] auto
  create_descriptor_set_layout(const DescriptorSetLayoutDesc &desc)
      -> DescriptorSetLayout;

  [[nodiscard]] auto
  allocate_descriptor_sets(const DescriptorPoolRef &pool,
                           std::span<const DescriptorSetLayoutRef> layouts,
                           std::span<VkDescriptorSet> sets) -> bool;
  [[nodiscard]] auto
  allocate_descriptor_set(const DescriptorPoolRef &pool,
                          const DescriptorSetLayoutRef &layout)
      -> Optional<VkDescriptorSet>;

  [[nodiscard]] auto
  allocate_descriptor_set(const DescriptorSetLayoutRef &layout)
      -> std::pair<DescriptorPool, VkDescriptorSet>;

  void write_descriptor_sets(std::span<const VkWriteDescriptorSet> configs);
  void write_descriptor_set(const VkWriteDescriptorSet &config);

  [[nodiscard]] auto create_buffer(BufferDesc desc) -> Buffer;
  [[nodiscard]] auto get_buffer_device_address(const BufferRef &buffer) const
      -> uint64_t;

  [[nodiscard]] auto create_texture(const TextureDesc &desc) -> Texture;
  void destroy_image_views(VkImage image);
  [[nodiscard]] VkImageView getVkImageView(const RenderTargetView &rtv);
  [[nodiscard]] VkImageView getVkImageView(const DepthStencilView &dsv);

  [[nodiscard]] auto create_pipeline_layout(const PipelineLayoutDesc &desc)
      -> PipelineLayout;

  [[nodiscard]] auto create_graphics_pipeline(GraphicsPipelineConfig config)
      -> GraphicsPipeline;

  [[nodiscard]] auto createBinarySemaphore() -> Semaphore;
  [[nodiscard]] auto createTimelineSemaphore(uint64_t initial_value = 0)
      -> VkSemaphore;

  [[nodiscard]] auto waitForSemaphore(VkSemaphore sem, uint64_t value,
                                      std::chrono::nanoseconds timeout) const
      -> SemaphoreWaitResult;
  void waitForSemaphore(VkSemaphore sem, uint64_t value) const {
    auto r = waitForSemaphore(sem, value, std::chrono::nanoseconds::max());
    assert(r == SemaphoreWaitResult::Ready);
  }

  [[nodiscard]] auto getSemaphoreValue(VkSemaphore semaphore) const -> uint64_t;

  [[nodiscard]] auto getGraphicsQueue() const -> VkQueue {
    return m_graphics_queue;
  }

  [[nodiscard]] auto getGraphicsQueueFamily() const -> unsigned {
    return m_graphics_queue_family;
  }

  [[nodiscard]] auto getGraphicsQueueSemaphore() const -> VkSemaphore {
    return m_graphics_queue_semaphore;
  }

  [[nodiscard]] auto getGraphicsQueueTime() const -> uint64_t {
    return m_graphics_queue_time;
  }

  [[nodiscard]] auto getGraphicsQueueCompletedTime() const -> uint64_t {
    return getSemaphoreValue(getGraphicsQueueSemaphore());
  }

  void graphicsQueueSubmit(std::span<const Submit> submits) {
    queueSubmitAndSignal(getGraphicsQueue(), submits,
                         getGraphicsQueueSemaphore(), ++m_graphics_queue_time);
  }

  void waitForGraphicsQueue(uint64_t time) const {
    waitForSemaphore(getGraphicsQueueSemaphore(), time);
  }

  [[nodiscard]] auto queuePresent(const VkPresentInfoKHR &present_info)
      -> VkResult;
};

} // namespace ren

struct RenDevice : ren::Device {
  using ren::Device::Device;
};
