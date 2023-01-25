#pragma once
#include "Config.hpp"
#include "Descriptors.hpp"
#include "DispatchTable.hpp"
#include "Pipeline.hpp"
#include "Reflection.hpp"
#include "Semaphore.hpp"
#include "Support/HashMap.hpp"
#include "Support/LinearMap.hpp"
#include "Support/Optional.hpp"
#include "Support/Queue.hpp"
#include "Support/TypeMap.hpp"
#include "VMA.hpp"

#include <chrono>
#include <functional>

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

class Device;

namespace detail {
template <typename T, size_t Idx, typename... Ts>
constexpr bool IsQueueTypeHelper = [] {
  if constexpr (Idx >= sizeof...(Ts)) {
    return false;
  } else if constexpr (std::same_as<
                           T, std::tuple_element_t<Idx, std::tuple<Ts...>>>) {
    return true;
  } else {
    return IsQueueTypeHelper<T, Idx + 1, Ts...>;
  }
}();
}

template <typename T, typename... Ts>
concept IsQueueType = detail::IsQueueTypeHelper<T, 0, Ts...>;

using QueueCustomDeleter = std::function<void(Device &device)>;

template <typename T> struct QueueDeleter {
  void operator()(Device &device, T value) const noexcept;
};

template <> struct QueueDeleter<QueueCustomDeleter> {
  void operator()(Device &device, QueueCustomDeleter deleter) const noexcept {
    deleter(device);
  }
};

namespace detail {
template <typename... Ts> class DeleteQueueImpl {
  struct FrameData {
    TypeMap<unsigned, Ts...> pushed_item_counts;
  };

  std::tuple<Queue<Ts>...> m_queues;
  std::array<FrameData, c_pipeline_depth> m_frame_data;
  unsigned m_frame_idx = 0;

private:
  template <typename T> Queue<T> &get_queue() {
    return std::get<Queue<T>>(m_queues);
  }
  template <typename T> unsigned &get_frame_pushed_item_count() {
    return m_frame_data[m_frame_idx].pushed_item_counts.template get<T>();
  }

  template <typename T> void push_impl(T value) {
    get_queue<T>().push(std::move(value));
    get_frame_pushed_item_count<T>()++;
  }

  template <typename T> void pop(Device &device, unsigned count) {
    auto &queue = get_queue<T>();
    for (int i = 0; i < count; ++i) {
      assert(not queue.empty());
      QueueDeleter<T>()(device, std::move(queue.front()));
      queue.pop();
    }
  }

public:
  void begin_frame(Device &device) {
    // Increment the frame index when a new frame is begin. If this is done when
    // a frame ends, shared_ptrs that go out of scope after will add their
    // resources to the frame that is about to begin and will be destroyed right
    // away, rather than after they are no longer in use.
    m_frame_idx = (m_frame_idx + 1) % c_pipeline_depth;
    (pop<Ts>(device, get_frame_pushed_item_count<Ts>()), ...);
    m_frame_data[m_frame_idx] = {};
  }
  void end_frame(Device &device) {}

  template <IsQueueType<Ts...> T> void push(T value) {
    push_impl(std::move(value));
  }

  template <std::convertible_to<QueueCustomDeleter> F>
    requires IsQueueType<QueueCustomDeleter, Ts...> and
             (not std::same_as<QueueCustomDeleter, F>)
  void push(F callback) {
    push_impl(QueueCustomDeleter(std::move(callback)));
  }

  void flush(Device &device) {
    (pop<Ts>(device, get_queue<Ts>().size()), ...);
    m_frame_data.fill({});
  }
};
} // namespace detail

struct ImageViews {
  VkImage image;
};

using DeleteQueue =
    detail::DeleteQueueImpl<QueueCustomDeleter, ImageViews, VkBuffer,
                            VkDescriptorPool, VkDescriptorSetLayout, VkImage,
                            VkPipeline, VkPipelineLayout, VkSemaphore,
                            VkSwapchainKHR, VmaAllocation>;

class Device : public InstanceFunctionsMixin<Device>,
               public PhysicalDeviceFunctionsMixin<Device>,
               public DeviceFunctionsMixin<Device> {
  VkInstance m_instance;
  VkPhysicalDevice m_adapter;
  VkDevice m_device;
  VmaAllocator m_allocator;
  DispatchTable m_vk = {};

  unsigned m_graphics_queue_family = -1;
  VkQueue m_graphics_queue;
  VkSemaphore m_graphics_queue_semaphore;
  uint64_t m_graphics_queue_time = 0;

  unsigned m_frame_index = 0;
  std::array<DeviceTime, c_pipeline_depth> m_frame_end_times = {};

  HashMap<VkImage, SmallLinearMap<TextureViewDesc, VkImageView, 3>>
      m_image_views;

  DeleteQueue m_delete_queue;

private:
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

  void flush();

  void begin_frame();
  void end_frame();

  static auto getRequiredAPIVersion() -> uint32_t { return VK_API_VERSION_1_3; }

  static auto getRequiredLayers() -> std::span<const char *const>;
  static auto getRequiredExtensions() -> std::span<const char *const>;

  auto getDispatchTable() const -> const DispatchTable & { return m_vk; }

  auto getInstance() const -> VkInstance { return m_instance; }

  auto getPhysicalDevice() const -> VkPhysicalDevice { return m_adapter; }

  auto getDevice() const -> VkDevice { return m_device; }

  auto getVMAAllocator() const -> VmaAllocator { return m_allocator; }

  auto getAllocator() const -> const VkAllocationCallbacks * { return nullptr; }

  template <typename T> void push_to_delete_queue(T value) {
    m_delete_queue.push(std::move(value));
  }

  [[nodiscard]] auto create_descriptor_set_layout(DescriptorSetLayoutDesc desc)
      -> DescriptorSetLayout;

  [[nodiscard]] auto create_descriptor_pool(const DescriptorPoolDesc &desc)
      -> DescriptorPool;

  void reset_descriptor_pool(const DescriptorPoolRef &pool);

  [[nodiscard]] auto
  allocate_descriptor_sets(const DescriptorPoolRef &pool,
                           std::span<const DescriptorSetLayoutRef> layouts,
                           VkDescriptorSet *sets) -> bool;
  [[nodiscard]] auto allocate_descriptor_set(const DescriptorPoolRef &pool,
                                             DescriptorSetLayoutRef layout)
      -> Optional<VkDescriptorSet>;

  [[nodiscard]] auto allocate_descriptor_set(DescriptorSetLayoutRef layout)
      -> std::pair<DescriptorPool, VkDescriptorSet>;

  void write_descriptor_sets(std::span<const VkWriteDescriptorSet> configs);
  void write_descriptor_set(const VkWriteDescriptorSet &config);

  [[nodiscard]] auto create_buffer(BufferDesc desc) -> Buffer;

  [[nodiscard]] auto create_texture(TextureDesc desc) -> Texture;
  void destroy_image_views(VkImage image);
  VkImageView getVkImageView(const TextureView &view);

  [[nodiscard]] auto create_pipeline_layout(PipelineLayoutDesc desc)
      -> PipelineLayout;

  [[nodiscard]] auto create_shader_module(std::span<const std::byte> code)
      -> SharedHandle<VkShaderModule>;
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

  auto getSemaphoreValue(VkSemaphore semaphore) const -> uint64_t;

  auto getGraphicsQueue() const -> VkQueue { return m_graphics_queue; }

  auto getGraphicsQueueFamily() const -> unsigned {
    return m_graphics_queue_family;
  }

  auto getGraphicsQueueSemaphore() const -> VkSemaphore {
    return m_graphics_queue_semaphore;
  }

  auto getGraphicsQueueTime() const -> uint64_t {
    return m_graphics_queue_time;
  }

  auto getGraphicsQueueCompletedTime() const -> uint64_t {
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
