#include "Renderer.hpp"
#include "SwapChain.hpp"
#include "core/Views.hpp"

#include <spirv/unified1/spirv.h>
#include <tracy/Tracy.hpp>

namespace ren {

struct ImageViewDesc {
  rhi::ImageViewDimension dimension = {};
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  rhi::ComponentMapping components;
  u32 base_mip = 0;
  u32 num_mips = 0;
  u32 base_layer = 0;
  u32 num_layers = 0;

public:
  bool operator==(const ImageViewDesc &) const = default;
};

struct ImageView {
  ImageViewDesc desc;
  rhi::ImageView handle;
};

struct alignas(64) ImageViewBlock {
  ImageViewBlock *next = nullptr;
  usize num_views = 0;
  ImageView views[9];
};

struct Sampler {
  rhi::SamplerCreateInfo desc;
  rhi::Sampler handle;
};

} // namespace ren

namespace ren_export {

expected<Renderer *> create_renderer(NotNull<Arena *> arena,
                                     const RendererInfo &info) {
  auto *renderer = arena->allocate<Renderer>();
  *renderer = {
      .m_arena = arena,
      .m_buffers = GenArray<Buffer>::init(arena),
      .m_textures = GenArray<Texture>::init(arena),
      .m_semaphores = GenArray<Semaphore>::init(arena),
      .m_events = GenArray<Event>::init(arena),
      .m_graphics_pipelines = GenArray<GraphicsPipeline>::init(arena),
      .m_compute_pipelines = GenArray<ComputePipeline>::init(arena),
      .m_command_pools = GenArray<CommandPool>::init(arena),
  };

  bool headless = info.type == RendererType::Headless;

  ren_try_to(rhi::load(headless));

  ren_try(renderer->m_instance,
          rhi::create_instance(renderer->m_arena, {
                                                      .debug_names = true,
#if REN_DEBUG_LAYER
                                                      .debug_layer = true,
#endif
                                                      .headless = headless,
                                                  }));

  if (info.adapter == DEFAULT_ADAPTER) {
    renderer->m_adapter = rhi::get_adapter_by_preference(
        renderer->m_instance, rhi::AdapterPreference::HighPerformance);
  } else {
    if (info.adapter >= rhi::get_adapter_count(renderer->m_instance)) {
      throw std::runtime_error("Vulkan: Failed to find requested adapter");
    }
    renderer->m_adapter = rhi::get_adapter(renderer->m_instance, info.adapter);
  }

  rhi::AdapterFeatures adapter_features =
      rhi::get_adapter_features(renderer->m_instance, renderer->m_adapter);

  ren_try(renderer->m_device,
          rhi::create_device(renderer->m_arena, renderer->m_instance,
                             {
                                 .adapter = renderer->m_adapter,
                                 .features = adapter_features,
                             }));

  if (adapter_features.amd_anti_lag) {
    renderer->m_features[(usize)RendererFeature::AmdAntiLag] = true;
  }

  return renderer;
}

void destroy_renderer(Renderer *renderer) {
  if (!renderer) {
    return;
  }

  ren_assert(renderer->m_command_pools.empty());
  for (rhi::CommandPool pool : renderer->m_cmd_pool_free_lists) {
    while (pool) {
      rhi::destroy_command_pool(renderer->m_device, pool);
      pool = pool->next;
    }
  }

  for (const Sampler &sampler : renderer->m_samplers) {
    rhi::destroy_sampler(renderer->m_device, sampler.handle);
  }
  rhi::destroy_device(renderer->m_device);
  rhi::destroy_instance(renderer->m_instance);
}

} // namespace ren_export

namespace ren {

auto Renderer::is_queue_family_supported(rhi::QueueFamily queue_family) const
    -> bool {
  return rhi::is_queue_family_supported(m_instance, m_adapter, queue_family);
}

void Renderer::wait_idle() { rhi::device_wait_idle(m_device).value(); }

auto Renderer::create_buffer(const BufferCreateInfo &create_info)
    -> Result<Handle<Buffer>, Error> {
  ren_assert(create_info.size > 0);
  ren_try(rhi::Buffer buffer,
          rhi::create_buffer(m_device, {
                                           .size = create_info.size,
                                           .heap = create_info.heap,
                                       }));
  rhi::set_debug_name(m_device, buffer, create_info.name);
  return m_buffers.insert(m_arena,
                          Buffer{
                              .handle = buffer,
                              .ptr = (std::byte *)rhi::map(m_device, buffer),
                              .address = rhi::get_device_ptr(m_device, buffer),
                              .size = create_info.size,
                              .heap = create_info.heap,
                          });
}

void Renderer::destroy(Handle<Buffer> handle) {
  if (Optional<Buffer> buffer = m_buffers.try_pop(handle)) {
    rhi::destroy_buffer(m_device, buffer->handle);
  };
}

auto Renderer::try_get_buffer(Handle<Buffer> buffer) const -> const Buffer * {
  return m_buffers.try_get(buffer);
};

auto Renderer::get_buffer(Handle<Buffer> buffer) const -> const Buffer & {
  ren_assert(m_buffers.contains(buffer));
  return m_buffers[buffer];
};

auto Renderer::try_get_buffer_view(Handle<Buffer> handle) const
    -> Optional<BufferView> {
  if (const Buffer *buffer = try_get_buffer(handle)) {
    return BufferView{
        .buffer = handle,
        .count = buffer->size,
    };
  };
  return {};
};

auto Renderer::get_buffer_view(Handle<Buffer> handle) const -> BufferView {
  const auto &buffer = get_buffer(handle);
  return {
      .buffer = handle,
      .count = buffer.size,
  };
};

auto Renderer::create_texture(const TextureCreateInfo &create_info)
    -> Result<Handle<Texture>, Error> {
  ren_assert(create_info.width > 0);
  ren_assert(create_info.num_mips > 0);

  ren_try(rhi::Image image,
          rhi::create_image(m_device, {
                                          .format = create_info.format,
                                          .usage = create_info.usage,
                                          .width = create_info.width,
                                          .height = create_info.height,
                                          .depth = create_info.depth,
                                          .cube_map = create_info.cube_map,
                                          .num_mips = create_info.num_mips,
                                          .num_layers = create_info.num_layers,
                                      }));
  rhi::set_debug_name(m_device, image, create_info.name);
  if (!m_image_view_free_list) {
    m_image_view_free_list = m_arena->allocate<ImageViewBlock>();
  }
  ImageViewBlock *views = m_image_view_free_list;
  m_image_view_free_list = m_image_view_free_list->next;
  views->next = nullptr;
  views->num_views = 0;
  return m_textures.insert(m_arena, Texture{
                                        .handle = image,
                                        .format = create_info.format,
                                        .usage = create_info.usage,
                                        .width = create_info.width,
                                        .height = create_info.height,
                                        .depth = create_info.depth,
                                        .cube_map = create_info.cube_map,
                                        .num_mips = create_info.num_mips,
                                        .num_layers = create_info.num_layers,
                                        .views = views,
                                    });
}

auto Renderer::create_external_texture(
    const ExternalTextureCreateInfo &create_info) -> Handle<Texture> {
  rhi::set_debug_name(m_device, create_info.handle, create_info.name);
  if (!m_image_view_free_list) {
    m_image_view_free_list = m_arena->allocate<ImageViewBlock>();
  }
  ImageViewBlock *views = m_image_view_free_list;
  m_image_view_free_list = m_image_view_free_list->next;
  views->next = nullptr;
  views->num_views = 0;
  return m_textures.insert(m_arena, Texture{
                                        .handle = create_info.handle,
                                        .format = create_info.format,
                                        .usage = create_info.usage,
                                        .width = create_info.width,
                                        .height = create_info.height,
                                        .depth = create_info.depth,
                                        .num_mips = create_info.num_mips,
                                        .num_layers = create_info.num_layers,
                                        .views = views,
                                    });
}

void Renderer::destroy(Handle<Texture> handle) {
  if (Optional<Texture> texture = m_textures.try_pop(handle)) {
    ImageViewBlock *last = nullptr;
    ImageViewBlock *views = texture->views;
    while (views) {
      for (const ImageView &view : Span(views->views, views->num_views)) {
        rhi::destroy_image_view(m_device, view.handle);
      }
      last = views;
      views = views->next;
    }
    last->next = m_image_view_free_list;
    m_image_view_free_list = texture->views;
    if (rhi::get_allocation(m_device, texture->handle)) {
      rhi::destroy_image(m_device, texture->handle);
    }
  };
}

auto Renderer::get_texture(Handle<Texture> texture) const -> const Texture & {
  ren_assert(m_textures.contains(texture));
  return m_textures[texture];
}

auto Renderer::get_srv(SrvDesc srv) -> Result<rhi::ImageView, Error> {
  return get_image_view(srv.texture, ImageViewDesc{
                                         .dimension = srv.dimension,
                                         .format = srv.format,
                                         .components = srv.components,
                                         .base_mip = srv.base_mip,
                                         .num_mips = srv.num_mips,
                                         .base_layer = srv.base_layer,
                                         .num_layers = srv.num_layers,
                                     });
}

auto Renderer::get_uav(UavDesc uav) -> Result<rhi::ImageView, Error> {
  return get_image_view(uav.texture, ImageViewDesc{
                                         .dimension = uav.dimension,
                                         .format = uav.format,
                                         .base_mip = uav.mip,
                                         .num_mips = 1,
                                         .base_layer = uav.base_layer,
                                         .num_layers = uav.num_layers,
                                     });
}

auto Renderer::get_rtv(RtvDesc rtv) -> Result<rhi::ImageView, Error> {
  return get_image_view(rtv.texture, ImageViewDesc{
                                         .dimension = rtv.dimension,
                                         .format = rtv.format,
                                         .base_mip = rtv.mip,
                                         .num_mips = 1,
                                         .base_layer = rtv.layer,
                                         .num_layers = 1,
                                     });
}

auto Renderer::get_image_view(Handle<Texture> handle, ImageViewDesc desc)
    -> Result<rhi::ImageView, Error> {
  Texture &texture = m_textures[handle];
  if (desc.format == TinyImageFormat_UNDEFINED) {
    desc.format = texture.format;
  }
  if (desc.num_mips == ALL_MIPS) {
    desc.num_mips = texture.num_mips - desc.base_mip;
  }
  if (desc.num_layers == ALL_LAYERS) {
    desc.num_layers = texture.num_layers - desc.base_layer;
  }

  ImageViewBlock *last = nullptr;
  ImageViewBlock *views = texture.views;
  while (views) {
    for (const ImageView &view : Span(views->views, views->num_views)) {
      if (view.desc == desc) {
        return view.handle;
      }
    }
    last = views;
    views = views->next;
  }
  if (last->num_views == std::size(last->views)) {
    if (!m_image_view_free_list) {
      m_image_view_free_list = m_arena->allocate<ImageViewBlock>();
    }
    last->next = m_image_view_free_list;
    m_image_view_free_list = m_image_view_free_list->next;
    last = last->next;
    last->next = nullptr;
    last->num_views = 0;
  }

  ren_try(rhi::ImageView view,
          rhi::create_image_view(
              m_device,
              {
                  .image = texture.handle,
                  .dimension = desc.dimension,
                  .format = desc.format,
                  .components = desc.components,
                  .aspect_mask = rhi::get_format_aspect_mask(texture.format),
                  .base_mip = desc.base_mip,
                  .num_mips = desc.num_mips,
                  .base_layer = desc.base_layer,
                  .num_layers = desc.num_layers,
              }));
  last->views[last->num_views++] = {desc, view};

  return view;
}

auto Renderer::get_sampler(const rhi::SamplerCreateInfo &create_info)
    -> Result<rhi::Sampler, Error> {
  for (const Sampler &sampler : m_samplers) {
    if (sampler.desc == create_info) {
      return sampler.handle;
    }
  }
  ren_try(rhi::Sampler sampler, rhi::create_sampler(m_device, create_info));
  m_samplers.push(m_arena, {create_info, sampler});
  return sampler;
}

auto Renderer::create_semaphore(const SemaphoreCreateInfo &create_info)
    -> Result<Handle<Semaphore>, Error> {
  ren_try(rhi::Semaphore semaphore,
          rhi::create_semaphore(m_device,
                                {
                                    .type = create_info.type,
                                    .initial_value = create_info.initial_value,
                                }));
  rhi::set_debug_name(m_device, semaphore, create_info.name);
  return m_semaphores.insert(m_arena, Semaphore{.handle = semaphore});
}

void Renderer::destroy(Handle<Semaphore> handle) {
  if (Optional<Semaphore> semaphore = m_semaphores.try_pop(handle)) {
    rhi::destroy_semaphore(m_device, semaphore->handle);
  }
}

auto Renderer::create_event() -> Handle<Event> {
  return m_events.insert(m_arena, Event{.handle = rhi::create_event(m_device)});
}

void Renderer::destroy(Handle<Event> handle) {
  if (Optional<Event> event = m_events.try_pop(handle)) {
    rhi::destroy_event(m_device, event->handle);
  }
}

auto Renderer::wait_for_semaphore(Handle<Semaphore> semaphore, u64 value,
                                  u64 timeout) const
    -> Result<rhi::WaitResult, Error> {
  return rhi::wait_for_semaphores(
      m_device, {{get_semaphore(semaphore).handle, value}}, timeout);
}

auto Renderer::wait_for_semaphore(Handle<Semaphore> semaphore, u64 value) const
    -> Result<void, Error> {
  ren_try(rhi::WaitResult wait_result,
          wait_for_semaphore(semaphore, value, UINT64_MAX));
  ren_assert(wait_result == rhi::WaitResult::Success);
  return {};
}

auto Renderer::get_semaphore(Handle<Semaphore> semaphore) const
    -> const Semaphore & {
  ren_assert(m_semaphores.contains(semaphore));
  return m_semaphores[semaphore];
}

auto Renderer::create_command_pool(const CommandPoolCreateInfo &create_info)
    -> Result<Handle<CommandPool>, Error> {
  auto &free_list = m_cmd_pool_free_lists[(i32)create_info.queue_family];
  if (!free_list) {
    ren_try(free_list,
            rhi::create_command_pool(
                m_arena, m_device, {.queue_family = create_info.queue_family}));
  }
  rhi::CommandPool pool = free_list;
  free_list = free_list->next;
  rhi::set_debug_name(m_device, pool, create_info.name);
  return m_command_pools.insert(m_arena,
                                CommandPool{
                                    .handle = pool,
                                    .queue_family = create_info.queue_family,
                                });
}

void Renderer::destroy(Handle<CommandPool> handle) {
  if (Optional<CommandPool> pool = m_command_pools.try_pop(handle)) {
    auto &free_list = m_cmd_pool_free_lists[(i32)pool->queue_family];
    pool->handle->next = free_list;
    free_list = pool->handle;
  }
}

auto Renderer::get_command_pool(Handle<CommandPool> pool)
    -> const CommandPool & {
  return m_command_pools[pool];
}

auto Renderer::reset_command_pool(Handle<CommandPool> pool)
    -> Result<void, Error> {
  return rhi::reset_command_pool(m_device, get_command_pool(pool).handle);
}

auto Renderer::create_graphics_pipeline(
    const GraphicsPipelineCreateInfo &create_info)
    -> Result<Handle<GraphicsPipeline>, Error> {
  ScratchArena scratch;

  u32 num_render_targets = 0;
  for (usize i : range(rhi::MAX_NUM_RENDER_TARGETS)) {
    if (create_info.rtv_formats[i]) {
      num_render_targets = i + 1;
    }
  }
  rhi::GraphicsPipelineCreateInfo pipeline_info = {
      .input_assembly_state = create_info.input_assembly_state,
      .rasterization_state = create_info.rasterization_state,
      .multisampling_state = create_info.multisampling_state,
      .depth_stencil_state = create_info.depth_stencil_state,
      .num_render_targets = num_render_targets,
      .dsv_format = create_info.dsv_format,
      .blend_state = create_info.blend_state,
  };
  std::ranges::copy(create_info.rtv_formats, pipeline_info.rtv_formats);

  const ShaderInfo *shaders[] = {
      &create_info.ts,
      &create_info.ms,
      &create_info.vs,
      &create_info.fs,
  };

  rhi::ShaderInfo *rhi_shaders[] = {
      &pipeline_info.ts,
      &pipeline_info.ms,
      &pipeline_info.vs,
      &pipeline_info.fs,
  };

  for (usize i : range(std::size(shaders))) {
    const ShaderInfo &shader = *shaders[i];
    u32 num_specialization_constants = shader.specialization_constants.size();
    auto *specialization_constants = allocate<rhi::SpecializationConstant>(
        scratch, num_specialization_constants);
    auto *specialization_data =
        allocate<u32>(scratch, num_specialization_constants);

    for (usize j : range(num_specialization_constants)) {
      const SpecializationConstant &c = shader.specialization_constants[j];
      specialization_constants[j] = {
          .id = c.id,
          .offset = u32(sizeof(u32) * j),
          .size = sizeof(u32),
      };
      specialization_data[j] = c.value;
    }
    *rhi_shaders[i] = {
        .code = shader.code,
        .entry_point = shader.entry_point,
        .specialization =
            {
                .constants = Span(specialization_constants,
                                  num_specialization_constants),
                .data = Span(specialization_data, num_specialization_constants)
                            .as_bytes(),
            },
    };
  }

  ren_try(rhi::Pipeline pipeline,
          rhi::create_graphics_pipeline(m_device, pipeline_info));
  rhi::set_debug_name(m_device, pipeline, create_info.name);

  return m_graphics_pipelines.insert(m_arena,
                                     GraphicsPipeline{.handle = pipeline});
}

void Renderer::destroy(Handle<GraphicsPipeline> handle) {
  if (Optional<GraphicsPipeline> pipeline =
          m_graphics_pipelines.try_pop(handle)) {
    rhi::destroy_pipeline(m_device, pipeline->handle);
  }
}

auto Renderer::get_graphics_pipeline(Handle<GraphicsPipeline> pipeline) const
    -> const GraphicsPipeline & {
  ren_assert(m_graphics_pipelines.contains(pipeline));
  return m_graphics_pipelines[pipeline];
}

auto Renderer::create_compute_pipeline(
    const ComputePipelineCreateInfo &create_info)
    -> Result<Handle<ComputePipeline>, Error> {
  ScratchArena scratch;

  Span<const std::byte> code = create_info.cs.code;

  Span<const u32> spirv((const u32 *)code.data(), code.size() / 4);
  ren_assert(spirv[0] == SpvMagicNumber);

  glm::uvec3 local_size = {};
  for (usize word = 5; word < spirv.size();) {
    usize num_words = spirv[word] >> SpvWordCountShift;
    SpvOp op = SpvOp(spirv[word] & SpvOpCodeMask);

    if (op == SpvOpExecutionMode) {
      SpvExecutionMode mode = SpvExecutionMode(spirv[word + 2]);
      if (mode == SpvExecutionModeLocalSize) {
        local_size.x = spirv[word + 3];
        local_size.y = spirv[word + 4];
        local_size.z = spirv[word + 5];
        break;
      }
    }

    word += num_words;
  }
  ren_assert(local_size != glm::uvec3(0));

  const ShaderInfo &cs = create_info.cs;

  u32 num_spec_consts = cs.specialization_constants.size();
  auto *specialization_constants =
      allocate<rhi::SpecializationConstant>(scratch, num_spec_consts);
  u32 *specialization_data = allocate<u32>(scratch, num_spec_consts);
  for (usize i : range(cs.specialization_constants.size())) {
    const SpecializationConstant &c = cs.specialization_constants[i];
    specialization_constants[i] = {
        .id = c.id,
        .offset = u32(sizeof(u32) * i),
        .size = sizeof(u32),
    };
    specialization_data[i] = c.value;
  }

  ren_try(
      rhi::Pipeline pipeline,
      rhi::create_compute_pipeline(
          m_device,
          {
              .cs =
                  {
                      .code = create_info.cs.code,
                      .entry_point = create_info.cs.entry_point,
                      .specialization =
                          {
                              .constants = Span(specialization_constants,
                                                num_spec_consts),
                              .data = Span(specialization_data, num_spec_consts)
                                          .as_bytes(),
                          },
                  },
          }));
  rhi::set_debug_name(m_device, pipeline, create_info.name);

  return m_compute_pipelines.insert(
      m_arena, ComputePipeline{
                   .handle = pipeline,
                   .local_size = {local_size.x, local_size.y, local_size.z},
               });
}

void Renderer::destroy(Handle<ComputePipeline> handle) {
  if (Optional<ComputePipeline> pipeline =
          m_compute_pipelines.try_pop(handle)) {
    rhi::destroy_pipeline(m_device, pipeline->handle);
  }
}

auto Renderer::get_compute_pipeline(Handle<ComputePipeline> pipeline) const
    -> const ComputePipeline & {
  ren_assert(m_compute_pipelines.contains(pipeline));
  return m_compute_pipelines[pipeline];
}

auto Renderer::submit(rhi::QueueFamily queue_family,
                      TempSpan<const rhi::CommandBuffer> cmd_buffers,
                      TempSpan<const SemaphoreState> wait_semaphores,
                      TempSpan<const SemaphoreState> signal_semaphores)
    -> Result<void, Error> {
  ScratchArena scratch;
  auto *wait_states =
      allocate<rhi::SemaphoreState>(scratch, wait_semaphores.size());
  for (usize i : range(wait_semaphores.size())) {
    wait_states[i] = {
        .semaphore = get_semaphore(wait_semaphores[i].semaphore).handle,
        .value = wait_semaphores[i].value,
    };
  }
  auto *signal_states =
      allocate<rhi::SemaphoreState>(scratch, signal_semaphores.size());
  for (usize i : range(signal_semaphores.size())) {
    signal_states[i] = {
        .semaphore = get_semaphore(signal_semaphores[i].semaphore).handle,
        .value = signal_semaphores[i].value,
    };
  }
  return rhi::queue_submit(rhi::get_queue(m_device, queue_family), cmd_buffers,
                           Span(wait_states, wait_semaphores.size()),
                           Span(signal_states, signal_semaphores.size()));
}

bool Renderer::is_feature_supported(RendererFeature feature) const {
  auto i = (usize)feature;
  ren_assert(i <= (usize)RendererFeature::Last);
  return m_features[i];
}

auto Renderer::amd_anti_lag_input(u64 frame, bool enable, u32 max_fps)
    -> Result<void, Error> {
  ZoneScoped;
  return rhi::amd_anti_lag_input(m_device, frame, enable, max_fps);
}

auto Renderer::amd_anti_lag_present(u64 frame, bool enable, u32 max_fps)
    -> Result<void, Error> {
  ZoneScoped;
  return rhi::amd_anti_lag_present(m_device, frame, enable, max_fps);
}

void unload(Renderer *renderer) { rhi::unload(renderer->m_instance); }

auto load(Renderer *renderer) -> Result<void, Error> {
  return rhi::load(renderer->m_instance);
}

} // namespace ren
