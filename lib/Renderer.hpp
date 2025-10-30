#pragma once
#include "Buffer.hpp"
#include "CommandPool.hpp"
#include "DevicePtr.hpp"
#include "Pipeline.hpp"
#include "Semaphore.hpp"
#include "Texture.hpp"
#include "ren/core/GenArray.hpp"
#include "ren/core/Span.hpp"
#include "rhi.hpp"

namespace ren {

struct Event {
  rhi::Event handle;
};

enum class RendererFeature {
  AmdAntiLag,
  Last = AmdAntiLag,
};

constexpr usize NUM_RENDERER_FEAUTURES = (usize)RendererFeature::Last + 1;

struct ImageViewDesc;
struct Sampler;

struct Renderer {
  Arena *m_arena = nullptr;
  rhi::Instance m_instance = {};
  rhi::Adapter m_adapter;
  rhi::Device m_device = {};

  bool m_features[NUM_RENDERER_FEAUTURES] = {};

  GenArray<Buffer> m_buffers;

  GenArray<Texture> m_textures;
  ImageViewBlock *m_image_view_free_list = nullptr;

  DynamicArray<Sampler> m_samplers;

  GenArray<Semaphore> m_semaphores;
  GenArray<Event> m_events;

  GenArray<GraphicsPipeline> m_graphics_pipelines;
  GenArray<ComputePipeline> m_compute_pipelines;

  GenArray<CommandPool> m_command_pools;
  rhi::CommandPool m_cmd_pool_free_lists[(i32)rhi::QueueFamily::Last + 1] = {};

public:
  auto get_adapter() const -> rhi::Adapter { return m_adapter; }

  auto get_rhi_device() const -> rhi::Device { return m_device; }

  auto is_queue_family_supported(rhi::QueueFamily queue_family) const -> bool;

  [[nodiscard]] auto create_buffer(const BufferCreateInfo &create_info)
      -> rhi::Result<Handle<Buffer>>;

  void destroy(Handle<Buffer> buffer);

  auto get_buffer(Handle<Buffer> buffer) const -> const Buffer &;

  auto get_buffer_view(Handle<Buffer> buffer) const -> BufferView;

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
    if (!handle) {
      return {};
    }
    const Buffer &buffer = get_buffer(handle);
    return DevicePtr<T>(buffer.address ? (buffer.address + map_offset) : 0);
  }

  template <typename T>
  auto try_get_buffer_device_ptr(const BufferSlice<T> &slice) const
      -> DevicePtr<T> {
    return try_get_buffer_device_ptr<T>(slice.buffer, slice.offset);
  }

  [[nodiscard]] auto create_texture(const TextureCreateInfo &create_info)
      -> rhi::Result<Handle<Texture>>;

  void destroy(Handle<Texture> texture);

  [[nodiscard]] auto
  create_external_texture(const ExternalTextureCreateInfo &create_info)
      -> Handle<Texture>;

  auto get_texture(Handle<Texture> texture) const -> const Texture &;

  rhi::ImageView get_srv(SrvDesc srv);

  rhi::ImageView get_uav(UavDesc uav);

  rhi::ImageView get_rtv(RtvDesc rtv);

  rhi::ImageView get_image_view(Handle<Texture> texture, ImageViewDesc view);

  [[nodiscard]] rhi::Sampler get_sampler(const rhi::SamplerCreateInfo &info);

  [[nodiscard]] Handle<GraphicsPipeline>
  create_graphics_pipeline(const GraphicsPipelineCreateInfo &create_info);

  void destroy(Handle<GraphicsPipeline> pipeline);

  auto get_graphics_pipeline(Handle<GraphicsPipeline> pipeline) const
      -> const GraphicsPipeline &;

  [[nodiscard]] Handle<ComputePipeline>
  create_compute_pipeline(const ComputePipelineCreateInfo &create_info);

  void destroy(Handle<ComputePipeline> pipeline);

  auto get_compute_pipeline(Handle<ComputePipeline> pipeline) const
      -> const ComputePipeline &;

  [[nodiscard]] Handle<Semaphore>
  create_semaphore(const SemaphoreCreateInfo &create_info);

  void destroy(Handle<Semaphore> semaphore);

  auto try_get_semaphore(Handle<Semaphore> semaphore) const
      -> const Semaphore * {
    return m_semaphores.try_get(semaphore);
  }

  auto get_semaphore(Handle<Semaphore> semaphore) const -> const Semaphore &;

  void wait_idle();

  [[nodiscard]] rhi::WaitResult
  wait_for_semaphore(Handle<Semaphore> semaphore, u64 value, u64 timeout) const;

  void wait_for_semaphore(Handle<Semaphore>, u64 value) const;

  auto create_event() -> Handle<Event>;
  void destroy(Handle<Event>);

  auto get_event(Handle<Event> event) -> const Event & {
    return m_events[event];
  }

  Handle<CommandPool>
  create_command_pool(const CommandPoolCreateInfo &create_info);

  void destroy(Handle<CommandPool> pool);

  auto get_command_pool(Handle<CommandPool> pool) -> const CommandPool &;

  void reset_command_pool(Handle<CommandPool> pool);

  void submit(rhi::QueueFamily queue_family,
              Span<const rhi::CommandBuffer> cmd_buffers,
              Span<const SemaphoreState> wait_semaphores = {},
              Span<const SemaphoreState> signal_semaphores = {});

  bool is_feature_supported(RendererFeature feature) const;

  void amd_anti_lag_input(u64 frame, bool enable = true, u32 max_fps = 0);

  void amd_anti_lag_present(u64 frame, bool enable = true, u32 max_fps = 0);

  void create_device();
};

[[nodiscard]] rhi::Result<void> load(Renderer *renderer);

} // namespace ren
