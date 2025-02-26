#include "Renderer.hpp"
#include "Profiler.hpp"
#include "Scene.hpp"
#include "Swapchain.hpp"
#include "core/Errors.hpp"
#include "core/Views.hpp"

#include <spirv_reflect.h>

namespace ren {

auto Renderer::init(u32 adapter) -> Result<void, Error> {
  ren_try(rhi::Features features, rhi::get_supported_features());

#if !REN_DEBUG_NAMES
  features.debug_names = false;
#endif

#if !REN_DEBUG_LAYER
  features.debug_layer = false;
#endif

  ren_try_to(rhi::init({.features = features}));

  if (adapter == DEFAULT_ADAPTER) {
    m_adapter =
        rhi::get_adapter_by_preference(rhi::AdapterPreference::HighPerformance);
  } else {
    if (adapter >= rhi::get_adapter_count()) {
      throw std::runtime_error("Vulkan: Failed to find requested adapter");
    }
    m_adapter = rhi::get_adapter(adapter);
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

auto Renderer::create_scene(ISwapchain &swapchain)
    -> expected<std::unique_ptr<IScene>> {
  return std::make_unique<Scene>(*this, static_cast<Swapchain &>(swapchain));
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
  m_buffers.try_pop(handle).map([&](const Buffer &buffer) {
    rhi::destroy_buffer(m_device, buffer.handle);
  });
}

auto Renderer::try_get_buffer(Handle<Buffer> buffer) const
    -> Optional<const Buffer &> {
  return m_buffers.try_get(buffer);
};

auto Renderer::get_buffer(Handle<Buffer> buffer) const -> const Buffer & {
  ren_assert(m_buffers.contains(buffer));
  return m_buffers[buffer];
};

auto Renderer::try_get_buffer_view(Handle<Buffer> handle) const
    -> Optional<BufferView> {
  return try_get_buffer(handle).map([&](const Buffer &buffer) -> BufferView {
    return {
        .buffer = handle,
        .count = buffer.size,
    };
  });
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
  ren_assert(create_info.num_mip_levels > 0);
  ren_assert(create_info.num_array_layers > 0);

  ren_try(rhi::Image image,
          rhi::create_image({
              .device = m_device,
              .format = create_info.format,
              .width = create_info.width,
              .height = create_info.height,
              .depth = create_info.depth,
              .num_mip_levels = create_info.num_mip_levels,
              .num_array_layers = create_info.num_array_layers,
              .usage = create_info.usage,
          }));
  ren_try_to(rhi::set_debug_name(m_device, image, create_info.name.c_str()));
  return m_textures.emplace(Texture{
      .handle = image,
      .format = create_info.format,
      .usage = create_info.usage,
      .width = create_info.width,
      .height = create_info.height,
      .depth = create_info.depth,
      .num_mip_levels = create_info.num_mip_levels,
      .num_array_layers = create_info.num_array_layers,
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
      .num_mip_levels = create_info.num_mip_levels,
      .num_array_layers = create_info.num_array_layers,
  });
}

void Renderer::destroy(Handle<Texture> handle) {
  m_textures.try_pop(handle).map([&](const Texture &texture) {
    if (rhi::get_allocation(m_device, texture.handle)) {
      rhi::destroy_image(m_device, texture.handle);
    }
    for (const auto &[desc, view] : m_image_views[handle]) {
      switch (desc.type) {
      case rhi::ImageViewType::SRV: {
        rhi::destroy_srv(m_device, view.srv);
      } break;
      case rhi::ImageViewType::UAV: {
        rhi::destroy_uav(m_device, view.uav);
      } break;
      case rhi::ImageViewType::RTV: {
        rhi::destroy_rtv(m_device, view.rtv);
      } break;
      }
    }
    m_image_views.erase(handle);
  });
}

auto Renderer::try_get_texture(Handle<Texture> texture) const
    -> Optional<const Texture &> {
  return m_textures.try_get(texture);
}

auto Renderer::get_texture(Handle<Texture> texture) const -> const Texture & {
  ren_assert(m_textures.contains(texture));
  return m_textures[texture];
}

auto Renderer::get_srv(SrvDesc srv) -> Result<rhi::SRV, Error> {
  ren_try(
      rhi::ImageView view,
      get_image_view(srv.texture, ImageViewDesc{
                                      .type = rhi::ImageViewType::SRV,
                                      .dimension = srv.dimension,
                                      .format = srv.format,
                                      .components = srv.components,
                                      .first_mip_level = srv.first_mip_level,
                                      .num_mip_levels = srv.num_mip_levels,
                                  }));
  return view.srv;
}

auto Renderer::get_uav(UavDesc uav) -> Result<rhi::UAV, Error> {
  ren_try(rhi::ImageView view,
          get_image_view(uav.texture, ImageViewDesc{
                                          .type = rhi::ImageViewType::UAV,
                                          .dimension = uav.dimension,
                                          .format = uav.format,
                                          .first_mip_level = uav.mip_level,
                                          .num_mip_levels = 1,
                                      }));
  return view.uav;
}

auto Renderer::get_rtv(RtvDesc rtv) -> Result<rhi::RTV, Error> {
  ren_try(rhi::ImageView view,
          get_image_view(rtv.texture, ImageViewDesc{
                                          .type = rhi::ImageViewType::RTV,
                                          .dimension = rtv.dimension,
                                          .format = rtv.format,
                                          .first_mip_level = rtv.mip_level,
                                          .num_mip_levels = 1,
                                      }));
  return view.rtv;
}

auto Renderer::get_image_view(Handle<Texture> handle, ImageViewDesc desc)
    -> Result<rhi::ImageView, Error> {
  const Texture &texture = get_texture(handle);
  if (desc.format == TinyImageFormat_UNDEFINED) {
    desc.format = texture.format;
  }
  if (desc.num_mip_levels == ALL_MIP_LEVELS) {
    desc.num_mip_levels = texture.num_mip_levels;
  }

  auto &image_views = m_image_views[handle];

  [[likely]] if (Optional<rhi::ImageView> image_view = image_views.get(desc)) {
    return *image_view;
  }

  rhi::ImageView view = {};
  switch (desc.type) {
  case rhi::ImageViewType::SRV: {
    ren_try(view.srv, rhi::create_srv(
                          m_device, {
                                        .image = texture.handle,
                                        .dimension = desc.dimension,
                                        .format = desc.format,
                                        .components = desc.components,
                                        .first_mip_level = desc.first_mip_level,
                                        .num_mip_levels = desc.num_mip_levels,
                                    }));
  } break;
  case rhi::ImageViewType::UAV: {
    ren_try(view.uav,
            rhi::create_uav(m_device, {
                                          .image = texture.handle,
                                          .dimension = desc.dimension,
                                          .format = desc.format,
                                          .mip_level = desc.first_mip_level,
                                      }));
  } break;
  case rhi::ImageViewType::RTV: {
    ren_try(view.rtv,
            rhi::create_rtv(m_device, {
                                          .image = texture.handle,
                                          .dimension = desc.dimension,
                                          .format = desc.format,
                                          .mip_level = desc.first_mip_level,
                                      }));
  } break;
  }

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

void Renderer::destroy(Handle<Sampler> sampler) {
  m_samplers.try_pop(sampler).map([&](const Sampler &sampler) {
    rhi::destroy_sampler(m_device, sampler.handle);
  });
}

auto Renderer::get_sampler(Handle<Sampler> sampler) const -> const Sampler & {
  ren_assert(m_samplers.contains(sampler));
  return m_samplers[sampler];
}

auto Renderer::create_resource_descriptor_heap(
    const ResourceDescriptorHeapCreateInfo &&create_info)
    -> Result<Handle<ResourceDescriptorHeap>, Error> {
  ren_try(rhi::ResourceDescriptorHeap heap,
          rhi::create_resource_descriptor_heap(
              m_device, {.num_descriptors = create_info.num_descriptors}));
  ren_try_to(rhi::set_debug_name(m_device, heap, create_info.name.c_str()));
  return m_resource_descriptor_heaps.emplace(ResourceDescriptorHeap{
      .handle = heap,
      .num_descriptors = create_info.num_descriptors,
  });
}

void Renderer::destroy(Handle<ResourceDescriptorHeap> heap) {
  m_resource_descriptor_heaps.try_pop(heap).transform(
      [&](const ResourceDescriptorHeap &heap) {
        rhi::destroy_resource_descriptor_heap(m_device, heap.handle);
      });
}

auto Renderer::get_resource_descriptor_heap(Handle<ResourceDescriptorHeap> heap)
    const -> const ResourceDescriptorHeap & {
  return m_resource_descriptor_heaps[heap];
}

auto Renderer::create_sampler_descriptor_heap(
    const SamplerDescriptorHeapCreateInfo &&create_info)
    -> Result<Handle<SamplerDescriptorHeap>, Error> {
  ren_try(rhi::SamplerDescriptorHeap heap,
          rhi::create_sampler_descriptor_heap(m_device));
  ren_try_to(rhi::set_debug_name(m_device, heap, create_info.name.c_str()));
  return m_sampler_descriptor_heaps.emplace(SamplerDescriptorHeap{
      .handle = heap,
  });
}

void Renderer::destroy(Handle<SamplerDescriptorHeap> heap) {
  m_sampler_descriptor_heaps.try_pop(heap).transform(
      [&](const SamplerDescriptorHeap &heap) {
        rhi::destroy_sampler_descriptor_heap(m_device, heap.handle);
      });
}

auto Renderer::get_sampler_descriptor_heap(
    Handle<SamplerDescriptorHeap> heap) const -> const SamplerDescriptorHeap & {
  return m_sampler_descriptor_heaps[heap];
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

void Renderer::destroy(Handle<Semaphore> semaphore) {
  m_semaphores.try_pop(semaphore).map([&](const Semaphore &semaphore) {
    rhi::destroy_semaphore(m_device, semaphore.handle);
  });
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

auto Renderer::try_get_semaphore(Handle<Semaphore> semaphore) const
    -> Optional<const Semaphore &> {
  return m_semaphores.try_get(semaphore);
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

void Renderer::destroy(Handle<CommandPool> pool) {
  m_command_pools.try_pop(pool).map([&](const CommandPool &pool) {
    rhi::destroy_command_pool(m_device, pool.handle);
  });
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
      .layout = get_pipeline_layout(create_info.layout).handle,
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

  return m_graphics_pipelines.emplace(GraphicsPipeline{
      .handle = pipeline,
      .layout = create_info.layout,
  });
}

void Renderer::destroy(Handle<GraphicsPipeline> pipeline) {
  m_graphics_pipelines.try_pop(pipeline).map(
      [&](const GraphicsPipeline &pipeline) {
        rhi::destroy_pipeline(m_device, pipeline.handle);
      });
}

auto Renderer::try_get_graphics_pipeline(Handle<GraphicsPipeline> pipeline)
    const -> Optional<const GraphicsPipeline &> {
  return m_graphics_pipelines.try_get(pipeline);
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
                  .layout = get_pipeline_layout(create_info.layout).handle,
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
      .layout = create_info.layout,
      .local_size = {local_size.x, local_size.y, local_size.z},
  });
}

void Renderer::destroy(Handle<ComputePipeline> pipeline) {
  m_compute_pipelines.try_pop(pipeline).map(
      [&](const ComputePipeline &pipeline) {
        rhi::destroy_pipeline(m_device, pipeline.handle);
      });
}

auto Renderer::try_get_compute_pipeline(Handle<ComputePipeline> pipeline) const
    -> Optional<const ComputePipeline &> {
  return m_compute_pipelines.try_get(pipeline);
}

auto Renderer::get_compute_pipeline(Handle<ComputePipeline> pipeline) const
    -> const ComputePipeline & {
  ren_assert(m_compute_pipelines.contains(pipeline));
  return m_compute_pipelines[pipeline];
}

auto Renderer::create_pipeline_layout(
    const PipelineLayoutCreateInfo &&create_info)
    -> Result<Handle<PipelineLayout>, Error> {
  ren_try(
      rhi::PipelineLayout layout,
      rhi::create_pipeline_layout(
          m_device, {
                        .use_resource_heap = create_info.use_resource_heap,
                        .use_sampler_heap = create_info.use_sampler_heap,
                        .push_descriptors = create_info.push_descriptors,
                        .push_constants_size = create_info.push_constants_size,
                    }));
  ren_try_to(rhi::set_debug_name(m_device, layout, create_info.name.c_str()));
  return m_pipeline_layouts.emplace(PipelineLayout{
      .handle = layout,
      .use_resource_heap = create_info.use_resource_heap,
      .use_sampler_heap = create_info.use_sampler_heap,
      .push_descriptors = create_info.push_descriptors,
      .push_constants_size = create_info.push_constants_size,
  });
}

void Renderer::destroy(Handle<PipelineLayout> layout) {
  m_pipeline_layouts.try_pop(layout).map([&](const PipelineLayout &layout) {
    rhi::destroy_pipeline_layout(m_device, layout.handle);
  });
}

auto Renderer::try_get_pipeline_layout(Handle<PipelineLayout> layout) const
    -> Optional<const PipelineLayout &> {
  return m_pipeline_layouts.try_get(layout);
}

auto Renderer::get_pipeline_layout(Handle<PipelineLayout> layout) const
    -> const PipelineLayout & {
  ren_assert(m_pipeline_layouts.contains(layout));
  return m_pipeline_layouts[layout];
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
  ren_prof_zone("AMD Anti-Lag (Input)");
  return rhi::amd_anti_lag_input(m_device, frame, enable, max_fps);
}

auto Renderer::amd_anti_lag_present(u64 frame, bool enable, u32 max_fps)
    -> Result<void, Error> {
  ren_prof_zone("AMD Anti-Lag (Present)");
  return rhi::amd_anti_lag_present(m_device, frame, enable, max_fps);
}

} // namespace ren
