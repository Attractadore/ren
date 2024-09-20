#pragma once
#include "Buffer.hpp"
#include "Descriptors.hpp"
#include "Pipeline.hpp"
#include "Semaphore.hpp"
#include "Support/GenArray.hpp"
#include "Support/HashMap.hpp"
#include "Support/LinearMap.hpp"
#include "Support/Optional.hpp"
#include "Support/Span.hpp"
#include "Texture.hpp"
#include "glsl/DevicePtr.hpp"

#include <volk.h>

#include <chrono>
#include <memory>

namespace ren {

struct SwapchainTextureCreateInfo;

class Renderer final : public IRenderer {
  VkInstance m_instance = nullptr;
#if REN_VULKAN_VALIDATION
  VkDebugReportCallbackEXT m_debug_callback = nullptr;
#endif
  VkPhysicalDevice m_adapter = nullptr;
  VkDevice m_device = nullptr;
  VmaAllocator m_allocator = nullptr;

  unsigned m_graphics_queue_family = -1;
  VkQueue m_graphics_queue = nullptr;

  GenArray<Buffer> m_buffers;

  GenArray<Texture> m_textures;

  HashMap<Handle<Texture>, SmallLinearMap<TextureView, VkImageView, 3>>
      m_image_views;

  GenArray<Sampler> m_samplers;

  GenArray<Semaphore> m_semaphores;

  GenArray<DescriptorPool> m_descriptor_pools;

  GenArray<DescriptorSetLayout> m_descriptor_set_layouts;

  GenArray<PipelineLayout> m_pipeline_layouts;

  GenArray<GraphicsPipeline> m_graphics_pipelines;
  GenArray<ComputePipeline> m_compute_pipelines;

public:
  Renderer(Span<const char *const> extensions, u32 adapter);
  Renderer(const Renderer &) = delete;
  Renderer(Renderer &&);
  Renderer &operator=(const Renderer &) = delete;
  Renderer &operator=(Renderer &&);
  ~Renderer();

  auto create_scene(ISwapchain &swapchain)
      -> expected<std::unique_ptr<IScene>> override;

  auto get_instance() const -> VkInstance { return m_instance; }

  auto get_adapter() const -> VkPhysicalDevice { return m_adapter; }

  auto get_device() const -> VkDevice { return m_device; }

  auto get_allocator() const -> VmaAllocator { return m_allocator; }

  [[nodiscard]] auto create_descriptor_set_layout(
      const DescriptorSetLayoutCreateInfo &&create_info)
      -> Handle<DescriptorSetLayout>;

  void destroy(Handle<DescriptorSetLayout> layout);

  auto try_get_descriptor_set_layout(Handle<DescriptorSetLayout> layout) const
      -> Optional<const DescriptorSetLayout &>;

  auto get_descriptor_set_layout(Handle<DescriptorSetLayout> layout) const
      -> const DescriptorSetLayout &;

  [[nodiscard]] auto create_descriptor_pool(
      const DescriptorPoolCreateInfo &&create_info) -> Handle<DescriptorPool>;

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
  [[nodiscard]] auto allocate_descriptor_set(
      Handle<DescriptorPool> pool,
      Handle<DescriptorSetLayout> layout) const -> Optional<VkDescriptorSet>;

  void
  write_descriptor_sets(TempSpan<const VkWriteDescriptorSet> configs) const;

  [[nodiscard]] auto
  create_buffer(const BufferCreateInfo &&create_info) -> Handle<Buffer>;

  void destroy(Handle<Buffer> buffer);

  auto try_get_buffer(Handle<Buffer> buffer) const -> Optional<const Buffer &>;

  auto get_buffer(Handle<Buffer> buffer) const -> const Buffer &;

  auto try_get_buffer_view(Handle<Buffer> buffer) const -> Optional<BufferView>;

  auto get_buffer_view(Handle<Buffer> buffer) const -> BufferView;

  template <typename T>
  auto try_get_buffer_slice(Handle<Buffer> buffer) const
      -> Optional<BufferSlice<T>> {
    try_get_buffer_view(buffer).map(
        [](const BufferView &view) { return BufferSlice<T>(view); });
  }

  template <typename T>
  auto get_buffer_slice(Handle<Buffer> buffer) const -> BufferSlice<T> {
    return BufferSlice<T>(get_buffer_view(buffer));
  }

  template <typename T = std::byte>
  auto map_buffer(Handle<Buffer> buffer, usize map_offset = 0) const -> T * {
    auto *ptr = get_buffer(buffer).ptr;
    if (!ptr)
      return nullptr;
    return (T *)(ptr + map_offset);
  }

  template <typename T>
  auto map_buffer(const BufferSlice<T> &slice) const -> T * {
    return map_buffer<T>(slice.buffer, slice.offset);
  }

  template <typename T>
  auto get_buffer_device_ptr(Handle<Buffer> buffer,
                             u64 map_offset = 0) const -> DevicePtr<T> {
    auto addr = get_buffer(buffer).address;
    return DevicePtr<T>(addr ? (addr + map_offset) : 0);
  }

  template <typename T>
  auto
  get_buffer_device_ptr(const BufferSlice<T> &slice) const -> DevicePtr<T> {
    return get_buffer_device_ptr<T>(slice.buffer, slice.offset);
  }

  template <typename T>
  auto try_get_buffer_device_ptr(Handle<Buffer> buffer,
                                 u64 map_offset = 0) const -> DevicePtr<T> {
    return try_get_buffer(buffer).map_or(
        [&](const Buffer &buffer) {
          return DevicePtr<T>(buffer.address ? (buffer.address + map_offset)
                                             : 0);
        },
        DevicePtr<T>());
  }

  template <typename T>
  auto
  try_get_buffer_device_ptr(const BufferSlice<T> &slice) const -> DevicePtr<T> {
    return try_get_buffer_device_ptr<T>(slice.buffer, slice.offset);
  }

  [[nodiscard]] auto
  create_texture(const TextureCreateInfo &&create_info) -> Handle<Texture>;

  void destroy(Handle<Texture> texture);

  [[nodiscard]] auto create_swapchain_texture(
      const SwapchainTextureCreateInfo &&create_info) -> Handle<Texture>;

  auto
  try_get_texture(Handle<Texture> texture) const -> Optional<const Texture &>;

  auto get_texture(Handle<Texture> texture) const -> const Texture &;

  auto
  try_get_texture_view(Handle<Texture> texture) const -> Optional<TextureView>;

  auto get_texture_view(Handle<Texture> texture) const -> TextureView;

  auto get_texture_view_size(const TextureView &view,
                             u32 mip_level_offset = 0) const -> glm::uvec3;

  auto getVkImageView(const TextureView &view) -> VkImageView;

  [[nodiscard]] auto
  create_sampler(const SamplerCreateInfo &&create_info) -> Handle<Sampler>;

  void destroy(Handle<Sampler> sampler);

  auto
  try_get_sampler(Handle<Sampler> sampler) const -> Optional<const Sampler &>;

  auto get_sampler(Handle<Sampler> sampler) const -> const Sampler &;

  [[nodiscard]] auto create_pipeline_layout(
      const PipelineLayoutCreateInfo &&create_info) -> Handle<PipelineLayout>;

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

  [[nodiscard]] auto create_compute_pipeline(
      const ComputePipelineCreateInfo &&create_info) -> Handle<ComputePipeline>;

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

  void wait_idle();

  [[nodiscard]] auto
  wait_for_semaphore(const Semaphore &semaphore, uint64_t value,
                     std::chrono::nanoseconds timeout) const -> VkResult;

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

  [[nodiscard]] auto
  queue_present(const VkPresentInfoKHR &present_info) -> VkResult;

private:
  template <typename H> friend class Handle;
};

} // namespace ren
