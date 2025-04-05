#pragma once
#include "Buffer.hpp"
#include "CommandPool.hpp"
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

#include <chrono>

namespace ren {

enum class RendererFeature {
  AmdAntiLag,
  Last = AmdAntiLag,
};

constexpr usize NUM_RENDERER_FEAUTURES = (usize)RendererFeature::Last + 1;

struct ImageViewDesc {
  rhi::ImageViewDimension dimension = {};
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  rhi::ComponentMapping components;
  u32 base_mip = 0;
  u32 num_mips = 0;
  u32 base_layer = 0;

public:
  bool operator==(const ImageViewDesc &) const = default;
};

class Renderer final : public IRenderer {
  rhi::Adapter m_adapter;
  rhi::Device m_device;

  BitSet<NUM_RENDERER_FEAUTURES> m_features;

  GenArray<Buffer> m_buffers;

  GenArray<Texture> m_textures;

  HashMap<Handle<Texture>, SmallLinearMap<ImageViewDesc, rhi::ImageView, 3>>
      m_image_views;

  GenArray<Sampler> m_samplers;

  GenArray<Semaphore> m_semaphores;

  GenArray<GraphicsPipeline> m_graphics_pipelines;
  GenArray<ComputePipeline> m_compute_pipelines;

  GenArray<CommandPool> m_command_pools;

public:
  Renderer() = default;
  Renderer(const Renderer &) = delete;
  Renderer(Renderer &&);
  Renderer &operator=(const Renderer &) = delete;
  Renderer &operator=(Renderer &&);
  ~Renderer();

  auto init(const RendererInfo &info) -> Result<void, Error>;

  auto get_adapter() const -> rhi::Adapter { return m_adapter; }

  auto get_rhi_device() const -> rhi::Device { return m_device; }

  auto is_queue_family_supported(rhi::QueueFamily queue_family) const -> bool;

  [[nodiscard]] auto create_buffer(const BufferCreateInfo &&create_info)
      -> Result<Handle<Buffer>, Error>;

  void destroy(Handle<Buffer> buffer);

  auto try_get_buffer(Handle<Buffer> buffer) const -> const Buffer *;

  auto get_buffer(Handle<Buffer> buffer) const -> const Buffer &;

  auto try_get_buffer_view(Handle<Buffer> buffer) const -> Optional<BufferView>;

  auto get_buffer_view(Handle<Buffer> buffer) const -> BufferView;

  template <typename T>
  auto try_get_buffer_slice(Handle<Buffer> buffer) const
      -> Optional<BufferSlice<T>> {
    try_get_buffer_view(buffer).transform(
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
  auto try_get_buffer_device_ptr(Handle<Buffer> handle,
                                 u64 map_offset = 0) const -> DevicePtr<T> {
    if (const Buffer *buffer = try_get_buffer(handle)) {
      return DevicePtr<T>(buffer->address ? (buffer->address + map_offset) : 0);
    }
    return {};
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

  auto get_texture(Handle<Texture> texture) const -> const Texture &;

  auto get_srv(SrvDesc srv) -> Result<rhi::ImageView, Error>;

  auto get_uav(UavDesc uav) -> Result<rhi::ImageView, Error>;

  auto get_rtv(RtvDesc rtv) -> Result<rhi::ImageView, Error>;

  auto get_image_view(Handle<Texture> texture, ImageViewDesc view)
      -> Result<rhi::ImageView, Error>;

  [[nodiscard]] auto create_sampler(const SamplerCreateInfo &&create_info)
      -> Result<Handle<Sampler>, Error>;

  void destroy(Handle<Sampler> sampler);

  auto try_get_sampler(Handle<Sampler> sampler) const
      -> Optional<const Sampler &>;

  auto get_sampler(Handle<Sampler> sampler) const -> const Sampler &;

  [[nodiscard]] auto
  create_graphics_pipeline(const GraphicsPipelineCreateInfo &&create_info)
      -> Result<Handle<GraphicsPipeline>, Error>;

  void destroy(Handle<GraphicsPipeline> pipeline);

  auto get_graphics_pipeline(Handle<GraphicsPipeline> pipeline) const
      -> const GraphicsPipeline &;

  [[nodiscard]] auto
  create_compute_pipeline(const ComputePipelineCreateInfo &&create_info)
      -> Result<Handle<ComputePipeline>, Error>;

  void destroy(Handle<ComputePipeline> pipeline);

  auto get_compute_pipeline(Handle<ComputePipeline> pipeline) const
      -> const ComputePipeline &;

  [[nodiscard]] auto create_semaphore(const SemaphoreCreateInfo &&create_info)
      -> Result<Handle<Semaphore>, Error>;

  void destroy(Handle<Semaphore> semaphore);

  auto try_get_semaphore(Handle<Semaphore> semaphore) const
      -> const Semaphore * {
    return m_semaphores.try_get(semaphore);
  }

  auto get_semaphore(Handle<Semaphore> semaphore) const -> const Semaphore &;

  void wait_idle();

  [[nodiscard]] auto wait_for_semaphore(Handle<Semaphore> semaphore, u64 value,
                                        std::chrono::nanoseconds timeout) const
      -> Result<rhi::WaitResult, Error>;

  [[nodiscard]] auto wait_for_semaphore(Handle<Semaphore>, u64 value) const
      -> Result<void, Error>;

  auto create_command_pool(const CommandPoolCreateInfo &create_info)
      -> Result<Handle<CommandPool>, Error>;

  void destroy(Handle<CommandPool> pool);

  auto get_command_pool(Handle<CommandPool> pool) -> const CommandPool &;

  auto reset_command_pool(Handle<CommandPool> pool) -> Result<void, Error>;

  auto submit(rhi::QueueFamily queue_family,
              TempSpan<const rhi::CommandBuffer> cmd_buffers,
              TempSpan<const SemaphoreState> wait_semaphores = {},
              TempSpan<const SemaphoreState> signal_semaphores = {})
      -> Result<void, Error>;

  bool is_feature_supported(RendererFeature feature) const;

  auto amd_anti_lag_input(u64 frame, bool enable = true, u32 max_fps = 0)
      -> Result<void, Error>;

  auto amd_anti_lag_present(u64 frame, bool enable = true, u32 max_fps = 0)
      -> Result<void, Error>;

private:
  void create_device();

private:
  template <typename H> friend class Handle;
};

} // namespace ren
