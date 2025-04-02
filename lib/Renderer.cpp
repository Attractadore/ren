#include "Renderer.hpp"
#include "Swapchain.hpp"
#include "core/Errors.hpp"
#include "core/Views.hpp"

#include <spirv_reflect.h>
#include <tracy/Tracy.hpp>

namespace ren {

auto Renderer::init(const RendererInfo &info) -> Result<void, Error> {
  ren_try_to(rhi::load(info.type == RendererType::Headless));

  ren_try(rhi::Features features, rhi::get_supported_features());

#if !REN_DEBUG_NAMES
  features.debug_names = false;
#endif

#if !REN_DEBUG_LAYER
  features.debug_layer = false;
#endif

  ren_try_to(rhi::init({
      .features = features,
  }));

  if (info.adapter == DEFAULT_ADAPTER) {
    m_adapter =
        rhi::get_adapter_by_preference(rhi::AdapterPreference::HighPerformance);
  } else {
    if (info.adapter >= rhi::get_adapter_count()) {
      throw std::runtime_error("Vulkan: Failed to find requested adapter");
    }
    m_adapter = rhi::get_adapter(info.adapter);
  }

  rhi::AdapterFeatures adapter_features = rhi::get_adapter_features(m_adapter);

  ren_try(m_device, rhi::create_device({
                        .adapter = m_adapter,
                        .features = adapter_features,
                    }));

  if (adapter_features.amd_anti_lag) {
    m_features[(usize)RendererFeature::AmdAntiLag] = true;
  }

  return {};
}

Renderer::~Renderer() {
  rhi::destroy_device(m_device);
  rhi::exit();
}

auto Renderer::is_queue_family_supported(rhi::QueueFamily queue_family) const
    -> bool {
  return rhi::is_queue_family_supported(m_adapter, queue_family);
}

void Renderer::wait_idle() { rhi::device_wait_idle(m_device).value(); }

auto Renderer::create_buffer(const BufferCreateInfo &&create_info)
    -> Result<Handle<Buffer>, Error> {
  ren_assert(create_info.size > 0);
  ren_try(rhi::Buffer buffer, rhi::create_buffer({
                                  .device = m_device,
                                  .size = create_info.size,
                                  .heap = create_info.heap,
                              }));
  ren_try_to(rhi::set_debug_name(m_device, buffer, create_info.name.c_str()));
  return m_buffers.emplace(Buffer{
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

auto Renderer::create_texture(const TextureCreateInfo &&create_info)
    -> Result<Handle<Texture>, Error> {
  ren_assert(create_info.width > 0);
  ren_assert(create_info.num_mips > 0);

  ren_try(rhi::Image image, rhi::create_image({
                                .device = m_device,
                                .format = create_info.format,
                                .usage = create_info.usage,
                                .width = create_info.width,
                                .height = create_info.height,
                                .depth = create_info.depth,
                                .cube_map = create_info.cube_map,
                                .num_mips = create_info.num_mips,
                            }));
  ren_try_to(rhi::set_debug_name(m_device, image, create_info.name.c_str()));
  return m_textures.emplace(Texture{
      .handle = image,
      .format = create_info.format,
      .usage = create_info.usage,
      .width = create_info.width,
      .height = create_info.height,
      .depth = create_info.depth,
      .cube_map = create_info.cube_map,
      .num_mips = create_info.num_mips,
  });
}

auto Renderer::create_external_texture(
    const ExternalTextureCreateInfo &&create_info) -> Handle<Texture> {
  std::ignore = rhi::set_debug_name(m_device, create_info.handle,
                                    create_info.name.c_str());
  return m_textures.emplace(Texture{
      .handle = create_info.handle,
      .format = create_info.format,
      .usage = create_info.usage,
      .width = create_info.width,
      .height = create_info.height,
      .depth = create_info.depth,
      .num_mips = create_info.num_mips,
  });
}

void Renderer::destroy(Handle<Texture> handle) {
  if (Optional<Texture> texture = m_textures.try_pop(handle)) {
    if (rhi::get_allocation(m_device, texture->handle)) {
      rhi::destroy_image(m_device, texture->handle);
    }
    for (const auto &[desc, view] : m_image_views[handle]) {
      rhi::destroy_image_view(m_device, view);
    }
    m_image_views.erase(handle);
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
                                     });
}

auto Renderer::get_uav(UavDesc uav) -> Result<rhi::ImageView, Error> {
  return get_image_view(uav.texture, ImageViewDesc{
                                         .dimension = uav.dimension,
                                         .format = uav.format,
                                         .base_mip = uav.mip,
                                         .num_mips = 1,
                                     });
}

auto Renderer::get_rtv(RtvDesc rtv) -> Result<rhi::ImageView, Error> {
  return get_image_view(rtv.texture, ImageViewDesc{
                                         .dimension = rtv.dimension,
                                         .format = rtv.format,
                                         .base_mip = rtv.mip,
                                         .num_mips = 1,
                                         .base_layer = rtv.layer,
                                     });
}

auto Renderer::get_image_view(Handle<Texture> handle, ImageViewDesc desc)
    -> Result<rhi::ImageView, Error> {
  const Texture &texture = get_texture(handle);
  if (desc.format == TinyImageFormat_UNDEFINED) {
    desc.format = texture.format;
  }
  if (desc.num_mips == ALL_MIPS) {
    desc.num_mips = texture.num_mips - desc.base_mip;
  }

  auto &image_views = m_image_views[handle];

  [[likely]] if (const rhi::ImageView *view = image_views.try_get(desc)) {
    return *view;
  }

  ren_try(rhi::ImageView view,
          rhi::create_image_view(m_device, {
                                               .image = texture.handle,
                                               .dimension = desc.dimension,
                                               .format = desc.format,
                                               .components = desc.components,
                                               .base_mip = desc.base_mip,
                                               .num_mips = desc.num_mips,
                                               .base_layer = desc.base_layer,
                                           }));

  image_views.insert(desc, view);

  return view;
}

auto Renderer::create_sampler(const SamplerCreateInfo &&create_info)
    -> Result<Handle<Sampler>, Error> {
  ren_try(rhi::Sampler sampler,
          rhi::create_sampler(rhi::SamplerCreateInfo{
              .device = m_device,
              .mag_filter = create_info.mag_filter,
              .min_filter = create_info.min_filter,
              .mipmap_mode = create_info.mipmap_mode,
              .address_mode_u = create_info.address_mode_u,
              .address_mode_v = create_info.address_mode_v,
              .address_mode_w = create_info.address_mode_w,
              .reduction_mode = create_info.reduction_mode,
              .max_anisotropy = create_info.anisotropy,
          }));
  return m_samplers.emplace(Sampler{
      .handle = sampler,
      .mag_filter = create_info.mag_filter,
      .min_filter = create_info.min_filter,
      .mipmap_mode = create_info.mipmap_mode,
      .address_mode_u = create_info.address_mode_u,
      .address_mode_v = create_info.address_mode_v,
      .address_mode_w = create_info.address_mode_w,
      .reduction_mode = create_info.reduction_mode,
      .anisotropy = create_info.anisotropy,
  });
}

void Renderer::destroy(Handle<Sampler> handle) {
  if (Optional<Sampler> sampler = m_samplers.try_pop(handle)) {
    rhi::destroy_sampler(m_device, sampler->handle);
  }
}

auto Renderer::get_sampler(Handle<Sampler> sampler) const -> const Sampler & {
  ren_assert(m_samplers.contains(sampler));
  return m_samplers[sampler];
}

auto Renderer::create_semaphore(const SemaphoreCreateInfo &&create_info)
    -> Result<Handle<Semaphore>, Error> {
  ren_try(rhi::Semaphore semaphore,
          rhi::create_semaphore({
              .device = m_device,
              .type = create_info.type,
              .initial_value = create_info.initial_value,
          }));
  return m_semaphores.emplace(Semaphore{.handle = semaphore});
}

void Renderer::destroy(Handle<Semaphore> handle) {
  if (Optional<Semaphore> semaphore = m_semaphores.try_pop(handle)) {
    rhi::destroy_semaphore(m_device, semaphore->handle);
  }
}

auto Renderer::wait_for_semaphore(Handle<Semaphore> semaphore, u64 value,
                                  std::chrono::nanoseconds timeout) const
    -> Result<rhi::WaitResult, Error> {
  return rhi::wait_for_semaphores(
      m_device, {{get_semaphore(semaphore).handle, value}}, timeout);
}

auto Renderer::wait_for_semaphore(Handle<Semaphore> semaphore, u64 value) const
    -> Result<void, Error> {
  ren_try(rhi::WaitResult wait_result,
          wait_for_semaphore(semaphore, value,
                             std::chrono::nanoseconds(UINT64_MAX)));
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
  ren_try(rhi::CommandPool pool,
          rhi::create_command_pool(m_device,
                                   {.queue_family = create_info.queue_family}));
  ren_try_to(rhi::set_debug_name(m_device, pool, create_info.name.c_str()));
  return m_command_pools.emplace(CommandPool{
      .handle = pool,
      .queue_family = create_info.queue_family,
  });
}

void Renderer::destroy(Handle<CommandPool> handle) {
  if (Optional<CommandPool> pool = m_command_pools.try_pop(handle)) {
    rhi::destroy_command_pool(m_device, pool->handle);
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
    const GraphicsPipelineCreateInfo &&create_info)
    -> Result<Handle<GraphicsPipeline>, Error> {
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

  SmallVector<rhi::SpecializationConstant> specialization_constants;
  SmallVector<u32> specialization_data;
  {
    u32 num_specialization_constants = 0;
    for (const ShaderInfo *shader : shaders) {
      num_specialization_constants += shader->specialization_constants.size();
    }
    specialization_constants.resize(num_specialization_constants);
    specialization_data.resize(num_specialization_constants);
  }

  u32 specialization_offset = 0;
  for (usize i : range(std::size(shaders))) {
    const ShaderInfo &shader = *shaders[i];
    u32 num_specialization_constants = shader.specialization_constants.size();
    for (usize j : range(num_specialization_constants)) {
      const SpecializationConstant &c = shader.specialization_constants[j];
      specialization_constants[specialization_offset + j] = {
          .id = c.id,
          .offset = u32(sizeof(u32) * j),
          .size = sizeof(u32),
      };
      specialization_data[specialization_offset + j] = c.value;
    }
    *rhi_shaders[i] = {
        .code = shader.code,
        .entry_point = shader.entry_point,
        .specialization =
            {
                .constants = Span(specialization_constants.data() +
                                      specialization_offset,
                                  num_specialization_constants),
                .data = Span(specialization_data.data() + specialization_offset,
                             num_specialization_constants)
                            .as_bytes(),
            },
    };
    specialization_offset += num_specialization_constants;
  }

  ren_try(rhi::Pipeline pipeline,
          rhi::create_graphics_pipeline(m_device, pipeline_info));
  ren_try_to(rhi::set_debug_name(m_device, pipeline, create_info.name.c_str()));

  return m_graphics_pipelines.emplace(GraphicsPipeline{.handle = pipeline});
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
    const ComputePipelineCreateInfo &&create_info)
    -> Result<Handle<ComputePipeline>, Error> {
  Span<const std::byte> code = create_info.cs.code;

  spv_reflect::ShaderModule shader(code.size_bytes(), code.data(),
                                   SPV_REFLECT_MODULE_FLAG_NO_COPY);
  throw_if_failed(shader.GetResult(),
                  "SPIRV-Reflect: Failed to create shader module");
  const SpvReflectEntryPoint *entry_point = spvReflectGetEntryPoint(
      &shader.GetShaderModule(), create_info.cs.entry_point);
  throw_if_failed(entry_point,
                  "SPIRV-Reflect: Failed to find entry point in shader module");
  SpvReflectEntryPoint::LocalSize local_size = entry_point->local_size;

  const ShaderInfo &cs = create_info.cs;

  SmallVector<rhi::SpecializationConstant> specialization_constants(
      cs.specialization_constants.size());
  SmallVector<u32> specialization_data(cs.specialization_constants.size());
  for (usize i : range(cs.specialization_constants.size())) {
    const SpecializationConstant &c = cs.specialization_constants[i];
    specialization_constants[i] = {
        .id = c.id,
        .offset = u32(sizeof(u32) * i),
        .size = sizeof(u32),
    };
    specialization_data[i] = c.value;
  }

  ren_try(rhi::Pipeline pipeline,
          rhi::create_compute_pipeline(
              m_device,
              {
                  .cs =
                      {
                          .code = create_info.cs.code,
                          .entry_point = create_info.cs.entry_point,
                          .specialization =
                              {
                                  .constants = specialization_constants,
                                  .data = Span(specialization_data).as_bytes(),
                              },
                      },
              }));
  ren_try_to(rhi::set_debug_name(m_device, pipeline, create_info.name.c_str()));

  return m_compute_pipelines.emplace(ComputePipeline{
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
  SmallVector<rhi::SemaphoreState> semaphore_states(wait_semaphores.size() +
                                                    signal_semaphores.size());
  for (usize i : range(wait_semaphores.size())) {
    semaphore_states[i] = {
        .semaphore = get_semaphore(wait_semaphores[i].semaphore).handle,
        .value = wait_semaphores[i].value,
    };
  }
  for (usize i : range(signal_semaphores.size())) {
    semaphore_states[wait_semaphores.size() + i] = {
        .semaphore = get_semaphore(signal_semaphores[i].semaphore).handle,
        .value = signal_semaphores[i].value,
    };
  }
  return rhi::queue_submit(
      rhi::get_queue(m_device, queue_family), cmd_buffers,
      Span(semaphore_states.data(), wait_semaphores.size()),
      Span(semaphore_states.data() + wait_semaphores.size(),
           signal_semaphores.size()));
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

} // namespace ren

auto ren::create_renderer(const RendererInfo &info)
    -> expected<std::unique_ptr<IRenderer>> {
  auto renderer = std::make_unique<Renderer>();
  return renderer->init(info).transform([&] { return std::move(renderer); });
}
