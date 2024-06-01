#pragma once
#include "Buffer.hpp"
#include "Config.hpp"
#include "Descriptors.hpp"
#include "Handle.hpp"
#include "HandleMap.hpp"
#include "Pipeline.hpp"
#include "Semaphore.hpp"
#include "Support/HashMap.hpp"
#include "Support/LinearMap.hpp"
#include "Support/Optional.hpp"
#include "Support/Queue.hpp"
#include "Support/Span.hpp"
#include "Support/TypeMap.hpp"
#include "Texture.hpp"
#include "glsl/BufferReference.hpp"

#include <volk.h>

#include <chrono>
#include <functional>
#include <memory>

namespace ren {

struct SwapchainTextureCreateInfo;

template <typename T, typename... Ts>
concept IsQueueType = (std::same_as<T, Ts> or ...);

using QueueCustomDeleter = std::function<void(Renderer &)>;

template <typename T> struct QueueDeleter;

template <> struct QueueDeleter<QueueCustomDeleter> {
  void operator()(Renderer &renderer,
                  QueueCustomDeleter deleter) const noexcept {
    std::move(deleter)(renderer);
  }
};

namespace detail {
template <typename... Ts> class DeleteQueueImpl {
  struct FrameData {
    TypeMap<unsigned, Ts...> pushed_item_counts;
  };

  std::tuple<Queue<Ts>...> m_queues;
  std::array<FrameData, PIPELINE_DEPTH> m_frame_data;
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

  template <typename T> void pop(Renderer &renderer, usize count) {
    auto &queue = get_queue<T>();
    for (usize i = 0; i < count; ++i) {
      ren_assert(not queue.empty());
      QueueDeleter<T>()(renderer, std::move(queue.front()));
      queue.pop();
    }
  }

public:
  void next_frame(Renderer &renderer) {
    m_frame_idx = (m_frame_idx + 1) % PIPELINE_DEPTH;
    (pop<Ts>(renderer, get_frame_pushed_item_count<Ts>()), ...);
    m_frame_data[m_frame_idx] = {};
  }

  template <IsQueueType<Ts...> T> void push(T value) {
    push_impl(std::move(value));
  }

  template <std::convertible_to<QueueCustomDeleter> F>
    requires IsQueueType<QueueCustomDeleter, Ts...> and
             (not std::same_as<QueueCustomDeleter, F>)
  void push(F callback) {
    push_impl(QueueCustomDeleter(std::move(callback)));
  }

  void flush(Renderer &renderer) {
    (pop<Ts>(renderer, get_queue<Ts>().size()), ...);
    m_frame_data.fill({});
  }
};
} // namespace detail

using DeleteQueue = detail::DeleteQueueImpl<
    QueueCustomDeleter, VkBuffer, VkDescriptorPool, VkDescriptorSetLayout,
    VkImageView, VkImage, VkPipeline, VkPipelineLayout, VkSampler, VkSemaphore,
    VkSwapchainKHR, // Swapchain must be destroyed before surface
    VkSurfaceKHR, VmaAllocation>;

struct InstanceDeleter {
  void operator()(VkInstance instance) const noexcept {
    vkDestroyInstance(instance, nullptr);
  }
};

struct DeviceDeleter {
  void operator()(VkDevice device) const noexcept {
    vkDestroyDevice(device, nullptr);
  }
};

struct AllocatorDeleter {
  void operator()(VmaAllocator allocator) const noexcept {
    vmaDestroyAllocator(allocator);
  }
};

template <typename T, typename D>
using UniqueHandle = std::unique_ptr<std::remove_pointer_t<T>, D>;

using UniqueInstance = UniqueHandle<VkInstance, InstanceDeleter>;
using UniqueDevice = UniqueHandle<VkDevice, DeviceDeleter>;
using UniqueAllocator = UniqueHandle<VmaAllocator, AllocatorDeleter>;

class Renderer final : public IRenderer {
  UniqueInstance m_instance;
  VkPhysicalDevice m_adapter = nullptr;
  UniqueDevice m_device;
  UniqueAllocator m_allocator;

  unsigned m_graphics_queue_family = -1;
  VkQueue m_graphics_queue = nullptr;
  Handle<Semaphore> m_graphics_queue_semaphore;
  uint64_t m_graphics_queue_time = 0;

  unsigned m_frame_index = 0;
  std::array<uint64_t, PIPELINE_DEPTH> m_frame_end_times = {};

  DeleteQueue m_delete_queue;

  HandleMap<Buffer> m_buffers;

  HandleMap<Texture> m_textures;

  HashMap<Handle<Texture>, SmallLinearMap<TextureView, VkImageView, 3>>
      m_image_views;

  HandleMap<Sampler> m_samplers;

  HandleMap<Semaphore> m_semaphores;

  HandleMap<DescriptorPool> m_descriptor_pools;

  HandleMap<DescriptorSetLayout> m_descriptor_set_layouts;

  HandleMap<PipelineLayout> m_pipeline_layouts;

  HandleMap<GraphicsPipeline> m_graphics_pipelines;
  HandleMap<ComputePipeline> m_compute_pipelines;

public:
  Renderer(Span<const char *const> extensions, u32 adapter);
  Renderer(const Renderer &) = delete;
  Renderer(Renderer &&);
  Renderer &operator=(const Renderer &) = delete;
  Renderer &operator=(Renderer &&);
  ~Renderer();

  auto create_scene(ISwapchain &swapchain)
      -> expected<std::unique_ptr<IScene>> override;

  void flush();

  void next_frame();

  static auto getRequiredAPIVersion() noexcept -> uint32_t {
    return VK_API_VERSION_1_3;
  }

  static auto getRequiredLayers() noexcept -> Span<const char *const>;
  static auto getInstanceExtensions() noexcept -> Span<const char *const>;

  auto get_instance() const -> VkInstance { return m_instance.get(); }

  auto get_adapter() const -> VkPhysicalDevice { return m_adapter; }

  auto get_device() const -> VkDevice { return m_device.get(); }

  auto get_allocator() const -> VmaAllocator { return m_allocator.get(); }

  template <typename T> void push_to_delete_queue(T value) {
    m_delete_queue.push(std::move(value));
  }

  [[nodiscard]] auto create_descriptor_set_layout(
      const DescriptorSetLayoutCreateInfo &&create_info)
      -> Handle<DescriptorSetLayout>;

  void destroy(Handle<DescriptorSetLayout> layout);

  auto try_get_descriptor_set_layout(Handle<DescriptorSetLayout> layout) const
      -> Optional<const DescriptorSetLayout &>;

  auto get_descriptor_set_layout(Handle<DescriptorSetLayout> layout) const
      -> const DescriptorSetLayout &;

  [[nodiscard]] auto
  create_descriptor_pool(const DescriptorPoolCreateInfo &&create_info)
      -> Handle<DescriptorPool>;

  void destroy(Handle<DescriptorPool>);

  auto try_get_descriptor_pool(Handle<DescriptorPool> layout) const
      -> Optional<const DescriptorPool &>;

  auto get_descriptor_pool(Handle<DescriptorPool> layout) const
      -> const DescriptorPool &;

  void reset_descriptor_pool(Handle<DescriptorPool> pool) const;

  [[nodiscard]] auto
  allocate_descriptor_sets(Handle<DescriptorPool> pool,
                           TempSpan<const Handle<DescriptorSetLayout>> layouts,
                           VkDescriptorSet *sets) const -> bool;
  [[nodiscard]] auto
  allocate_descriptor_set(Handle<DescriptorPool> pool,
                          Handle<DescriptorSetLayout> layout) const
      -> Optional<VkDescriptorSet>;

  void
  write_descriptor_sets(TempSpan<const VkWriteDescriptorSet> configs) const;

  [[nodiscard]] auto create_buffer(const BufferCreateInfo &&create_info)
      -> Handle<Buffer>;

  void destroy(Handle<Buffer> buffer);

  auto try_get_buffer(Handle<Buffer> buffer) const -> Optional<const Buffer &>;

  auto get_buffer(Handle<Buffer> buffer) const -> const Buffer &;

  auto try_get_buffer_view(Handle<Buffer> buffer) const -> Optional<BufferView>;

  auto get_buffer_view(Handle<Buffer> buffer) const -> BufferView;

  template <typename T = std::byte>
  auto map_buffer(Handle<Buffer> buffer, usize map_offset = 0) const -> T * {
    auto *ptr = get_buffer(buffer).ptr;
    if (!ptr)
      return nullptr;
    return (T *)(ptr + map_offset);
  }

  template <typename T = std::byte>
  auto map_buffer(const BufferView &view, usize map_offset = 0) const -> T * {
    return map_buffer<T>(view.buffer, view.offset + map_offset);
  }

  template <typename T>
  auto get_buffer_device_address(Handle<Buffer> buffer,
                                 u64 map_offset = 0) const
      -> BufferReference<T> {
    auto addr = get_buffer(buffer).address;
    return BufferReference<T>(addr ? (addr + map_offset) : 0);
  }

  template <typename T>
  auto try_get_buffer_device_address(Handle<Buffer> buffer,
                                     u64 map_offset = 0) const
      -> BufferReference<T> {
    return try_get_buffer(buffer).map_or(
        [&](const Buffer &buffer) {
          return BufferReference<T>(
              buffer.address ? (buffer.address + map_offset) : 0);
        },
        BufferReference<T>());
  }

  template <typename T>
  auto get_buffer_device_address(const BufferView &view,
                                 u64 map_offset = 0) const
      -> BufferReference<T> {
    return get_buffer_device_address<T>(view.buffer, view.offset + map_offset);
  }

  [[nodiscard]] auto create_texture(const TextureCreateInfo &&create_info)
      -> Handle<Texture>;

  void destroy(Handle<Texture> texture);

  [[nodiscard]] auto
  create_swapchain_texture(const SwapchainTextureCreateInfo &&create_info)
      -> Handle<Texture>;

  auto try_get_texture(Handle<Texture> texture) const
      -> Optional<const Texture &>;

  auto get_texture(Handle<Texture> texture) const -> const Texture &;

  auto try_get_texture_view(Handle<Texture> texture) const
      -> Optional<TextureView>;

  auto get_texture_view(Handle<Texture> texture) const -> TextureView;

  auto get_texture_view_size(const TextureView &view,
                             u32 mip_level_offset = 0) const -> glm::uvec3;

  auto getVkImageView(const TextureView &view) -> VkImageView;

  [[nodiscard]] auto create_sampler(const SamplerCreateInfo &&create_info)
      -> Handle<Sampler>;

  void destroy(Handle<Sampler> sampler);

  auto try_get_sampler(Handle<Sampler> sampler) const
      -> Optional<const Sampler &>;

  auto get_sampler(Handle<Sampler> sampler) const -> const Sampler &;

  [[nodiscard]] auto
  create_pipeline_layout(const PipelineLayoutCreateInfo &&create_info)
      -> Handle<PipelineLayout>;

  void destroy(Handle<PipelineLayout> layout);

  auto try_get_pipeline_layout(Handle<PipelineLayout> layout) const
      -> Optional<const PipelineLayout &>;

  auto get_pipeline_layout(Handle<PipelineLayout> layout) const
      -> const PipelineLayout &;

  [[nodiscard]] auto
  create_graphics_pipeline(const GraphicsPipelineCreateInfo &&create_info)
      -> Handle<GraphicsPipeline>;

  void destroy(Handle<GraphicsPipeline> pipeline);

  auto try_get_graphics_pipeline(Handle<GraphicsPipeline> pipeline) const
      -> Optional<const GraphicsPipeline &>;

  auto get_graphics_pipeline(Handle<GraphicsPipeline> pipeline) const
      -> const GraphicsPipeline &;

  [[nodiscard]] auto
  create_compute_pipeline(const ComputePipelineCreateInfo &&create_info)
      -> Handle<ComputePipeline>;

  void destroy(Handle<ComputePipeline> pipeline);

  auto try_get_compute_pipeline(Handle<ComputePipeline> pipeline) const
      -> Optional<const ComputePipeline &>;

  auto get_compute_pipeline(Handle<ComputePipeline> pipeline) const
      -> const ComputePipeline &;

  [[nodiscard]] auto create_semaphore(const SemaphoreCreateInfo &&create_info)
      -> Handle<Semaphore>;

  void destroy(Handle<Semaphore> semaphore);

  auto try_get_semaphore(Handle<Semaphore> semaphore) const
      -> Optional<const Semaphore &>;

  auto get_semaphore(Handle<Semaphore> semaphore) const -> const Semaphore &;

  [[nodiscard]] auto wait_for_semaphore(const Semaphore &semaphore,
                                        uint64_t value,
                                        std::chrono::nanoseconds timeout) const
      -> VkResult;

  void wait_for_semaphore(const Semaphore &semaphore, uint64_t value) const;

  auto getGraphicsQueue() const -> VkQueue { return m_graphics_queue; }

  auto get_graphics_queue_family() const -> unsigned {
    return m_graphics_queue_family;
  }

  void graphicsQueueSubmit(
      TempSpan<const VkCommandBufferSubmitInfo> cmd_buffers,
      TempSpan<const VkSemaphoreSubmitInfo> wait_semaphores = {},
      TempSpan<const VkSemaphoreSubmitInfo> signal_semaphores = {}) {
    queueSubmit(getGraphicsQueue(), cmd_buffers, wait_semaphores,
                signal_semaphores);
  }

  void
  queueSubmit(VkQueue queue,
              TempSpan<const VkCommandBufferSubmitInfo> cmd_buffers,
              TempSpan<const VkSemaphoreSubmitInfo> wait_semaphores = {},
              TempSpan<const VkSemaphoreSubmitInfo> signal_semaphores = {});

  [[nodiscard]] auto queue_present(const VkPresentInfoKHR &present_info)
      -> VkResult;

private:
  template <typename H> friend class Handle;
};

} // namespace ren
