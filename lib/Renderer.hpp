#pragma once
#include "Buffer.hpp"
#include "Descriptors.hpp"
#include "Pipeline.hpp"
#include "Semaphore.hpp"
#include "Texture.hpp"
#include "core/BitSet.hpp"
#include "core/GenArray.hpp"
#include "core/HashMap.hpp"
#include "core/LinearMap.hpp"
#include "core/Optional.hpp"
#include "core/Span.hpp"
#include "glsl/DevicePtr.hpp"
#include "rhi.hpp"

#include <volk.h>

#include <chrono>
#include <memory>

namespace ren {

enum class RendererFeature {
  AmdAntiLag,
  Last = AmdAntiLag,
};

constexpr usize NUM_RENDERER_FEAUTURES = (usize)RendererFeature::Last + 1;

struct ImageViewDesc {
  rhi::ImageViewType type = {};
  rhi::ImageViewDimension dimension = {};
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  rhi::ComponentMapping components;
  u32 first_mip_level = 0;
  u32 num_mip_levels = 0;

public:
  bool operator==(const ImageViewDesc &) const = default;
};

class Renderer final : public IRenderer {
  rhi::Adapter m_adapter;
  rhi::Device m_device;
  rhi::Queue m_graphics_queue;

  BitSet<NUM_RENDERER_FEAUTURES> m_features;

  GenArray<Buffer> m_buffers;

  GenArray<Texture> m_textures;

  HashMap<Handle<Texture>, SmallLinearMap<ImageViewDesc, rhi::ImageView, 3>>
      m_image_views;

  GenArray<Sampler> m_samplers;

  GenArray<Semaphore> m_semaphores;

  GenArray<ResourceDescriptorHeap> m_resource_descriptor_heaps;
  GenArray<SamplerDescriptorHeap> m_sampler_descriptor_heaps;

  GenArray<PipelineLayout> m_pipeline_layouts;

  GenArray<GraphicsPipeline> m_graphics_pipelines;
  GenArray<ComputePipeline> m_compute_pipelines;

public:
  Renderer() = default;
  Renderer(const Renderer &) = delete;
  Renderer(Renderer &&);
  Renderer &operator=(const Renderer &) = delete;
  Renderer &operator=(Renderer &&);
  ~Renderer();

  auto init(u32 adapter) -> Result<void, Error>;

  auto create_scene(ISwapchain &swapchain)
      -> expected<std::unique_ptr<IScene>> override;

  auto get_adapter() const -> rhi::Adapter { return m_adapter; }

  auto get_rhi_device() const -> rhi::Device { return m_device; }

  auto get_device() const -> VkDevice {
    return rhi::vk::get_vk_device(m_device);
  }

  auto get_allocator() const -> VmaAllocator {
    return rhi::vk::get_vma_allocator(m_device);
  }

  [[nodiscard]] auto create_buffer(const BufferCreateInfo &&create_info)
      -> Result<Handle<Buffer>, Error>;

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
  auto get_buffer_device_ptr(Handle<Buffer> buffer, u64 map_offset = 0) const
      -> DevicePtr<T> {
    auto addr = get_buffer(buffer).address;
    return DevicePtr<T>(addr ? (addr + map_offset) : 0);
  }

  template <typename T>
  auto get_buffer_device_ptr(const BufferSlice<T> &slice) const
      -> DevicePtr<T> {
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
  auto try_get_buffer_device_ptr(const BufferSlice<T> &slice) const
      -> DevicePtr<T> {
    return try_get_buffer_device_ptr<T>(slice.buffer, slice.offset);
  }

  [[nodiscard]] auto create_texture(const TextureCreateInfo &&create_info)
      -> Result<Handle<Texture>, Error>;

  void destroy(Handle<Texture> texture);

  [[nodiscard]] auto
  create_external_texture(const ExternalTextureCreateInfo &&create_info)
      -> Handle<Texture>;

  auto try_get_texture(Handle<Texture> texture) const
      -> Optional<const Texture &>;

  auto get_texture(Handle<Texture> texture) const -> const Texture &;

  auto get_srv(SrvDesc srv) -> Result<rhi::SRV, Error>;

  auto get_uav(UavDesc uav) -> Result<rhi::UAV, Error>;

  auto get_rtv(RtvDesc rtv) -> Result<rhi::RTV, Error>;

  auto get_image_view(Handle<Texture> texture, ImageViewDesc view)
      -> Result<rhi::ImageView, Error>;

  [[nodiscard]] auto create_sampler(const SamplerCreateInfo &&create_info)
      -> Result<Handle<Sampler>, Error>;

  void destroy(Handle<Sampler> sampler);

  auto try_get_sampler(Handle<Sampler> sampler) const
      -> Optional<const Sampler &>;

  auto get_sampler(Handle<Sampler> sampler) const -> const Sampler &;

  [[nodiscard]] auto create_resource_descriptor_heap(
      const ResourceDescriptorHeapCreateInfo &&create_info)
      -> Result<Handle<ResourceDescriptorHeap>, Error>;

  void destroy(Handle<ResourceDescriptorHeap> heap);

  auto get_resource_descriptor_heap(Handle<ResourceDescriptorHeap> heap) const
      -> const ResourceDescriptorHeap &;

  [[nodiscard]] auto create_sampler_descriptor_heap(
      const SamplerDescriptorHeapCreateInfo &&create_info)
      -> Result<Handle<SamplerDescriptorHeap>, Error>;

  void destroy(Handle<SamplerDescriptorHeap> heap);

  auto get_sampler_descriptor_heap(Handle<SamplerDescriptorHeap> heap) const
      -> const SamplerDescriptorHeap &;

  [[nodiscard]] auto
  create_pipeline_layout(const PipelineLayoutCreateInfo &&create_info)
      -> Result<Handle<PipelineLayout>, Error>;

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
      -> Result<Handle<Semaphore>, Error>;

  void destroy(Handle<Semaphore> semaphore);

  auto try_get_semaphore(Handle<Semaphore> semaphore) const
      -> Optional<const Semaphore &>;

  auto get_semaphore(Handle<Semaphore> semaphore) const -> const Semaphore &;

  void wait_idle();

  [[nodiscard]] auto wait_for_semaphore(Handle<Semaphore> semaphore, u64 value,
                                        std::chrono::nanoseconds timeout) const
      -> Result<rhi::WaitResult, Error>;

  [[nodiscard]] auto wait_for_semaphore(Handle<Semaphore>, u64 value) const
      -> Result<void, Error>;

  auto getGraphicsQueue() const -> rhi::Queue { return m_graphics_queue; }

  auto get_graphics_queue_family() const -> unsigned {
    return rhi::vk::get_queue_family_index(m_adapter,
                                           rhi::QueueFamily::Graphics);
  }

  void graphicsQueueSubmit(
      TempSpan<const VkCommandBufferSubmitInfo> cmd_buffers,
      TempSpan<const VkSemaphoreSubmitInfo> wait_semaphores = {},
      TempSpan<const VkSemaphoreSubmitInfo> signal_semaphores = {}) {
    queueSubmit(getGraphicsQueue(), cmd_buffers, wait_semaphores,
                signal_semaphores);
  }

  void
  queueSubmit(rhi::Queue queue,
              TempSpan<const VkCommandBufferSubmitInfo> cmd_buffers,
              TempSpan<const VkSemaphoreSubmitInfo> wait_semaphores = {},
              TempSpan<const VkSemaphoreSubmitInfo> signal_semaphores = {});

  bool is_feature_supported(RendererFeature feature) const;

  void amd_anti_lag(u64 frame, VkAntiLagStageAMD stage, u32 max_fps = 0,
                    VkAntiLagModeAMD = VK_ANTI_LAG_MODE_ON_AMD);

private:
  void create_device();

private:
  template <typename H> friend class Handle;
};

} // namespace ren
