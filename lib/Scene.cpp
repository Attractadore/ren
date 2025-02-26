#include "Scene.hpp"
#include "CommandRecorder.hpp"
#include "ImGuiConfig.hpp"
#include "MeshProcessing.hpp"
#include "Profiler.hpp"
#include "Swapchain.hpp"
#include "core/Span.hpp"
#include "core/Views.hpp"
#include "passes/Exposure.hpp"
#include "passes/GpuSceneUpdate.hpp"
#include "passes/ImGui.hpp"
#include "passes/Opaque.hpp"
#include "passes/PostProcessing.hpp"
#include "passes/Present.hpp"

#include <fmt/format.h>

namespace ren {

Scene::Scene(Renderer &renderer, Swapchain &swapchain)
    : m_arena(renderer)

{
  m_renderer = &renderer;
  m_swapchain = &swapchain;

  m_descriptor_allocator.init(m_arena).value();

  m_samplers = {
      .hi_z_gen =
          m_arena
              .create_sampler({
                  .mag_filter = rhi::Filter::Linear,
                  .min_filter = rhi::Filter::Linear,
                  .mipmap_mode = rhi::SamplerMipmapMode::Nearest,
                  .address_mode_u = rhi::SamplerAddressMode::ClampToEdge,
                  .address_mode_v = rhi::SamplerAddressMode::ClampToEdge,
                  .reduction_mode = rhi::SamplerReductionMode::Min,
              })
              .value(),
      .hi_z = m_arena
                  .create_sampler({
                      .mag_filter = rhi::Filter::Nearest,
                      .min_filter = rhi::Filter::Nearest,
                      .mipmap_mode = rhi::SamplerMipmapMode::Nearest,
                      .address_mode_u = rhi::SamplerAddressMode::ClampToEdge,
                      .address_mode_v = rhi::SamplerAddressMode::ClampToEdge,
                  })
                  .value(),
  };

  m_pipelines = load_pipelines(m_arena).value();

  m_rgp = std::make_unique<RgPersistent>(*m_renderer);

  m_gpu_scene = init_gpu_scene(m_arena);

  m_data.settings.async_compute =
      m_renderer->is_queue_family_supported(rhi::QueueFamily::Compute);
  m_data.settings.present_from_compute =
      m_swapchain->is_queue_family_supported(rhi::QueueFamily::Compute);

  m_data.settings.amd_anti_lag =
      m_renderer->is_feature_supported(RendererFeature::AmdAntiLag);

  allocate_per_frame_resources().value();

  m_gfx_allocator.init(renderer, m_arena, 256 * MiB);
  if (m_renderer->is_queue_family_supported(rhi::QueueFamily::Compute)) {
    m_async_allocator.init(renderer, m_arena, 16 * MiB);
    for (DeviceBumpAllocator &allocator : m_shared_allocators) {
      allocator.init(renderer, m_arena, 16 * MiB);
    }
  }

  next_frame().value();
}

auto Scene::allocate_per_frame_resources() -> Result<void, Error> {
  for (auto i : range(NUM_FRAMES_IN_FLIGHT)) {
    ren_try(Handle<Semaphore> acquire_semaphore,
            m_arena.create_semaphore({
                .name = fmt::format("Acquire semaphore {}", i),
                .type = rhi::SemaphoreType::Binary,
            }));
    ren_try(Handle<Semaphore> present_semaphore,
            m_arena.create_semaphore({
                .name = fmt::format("Present semaphore {}", i),
                .type = rhi::SemaphoreType::Binary,
            }));
    m_per_frame_resources.emplace_back(ScenePerFrameResources{
        .acquire_semaphore = acquire_semaphore,
        .present_semaphore = present_semaphore,
        .descriptor_allocator =
            DescriptorAllocatorScope(m_descriptor_allocator),
    });
    ScenePerFrameResources &frcs = m_per_frame_resources.back();
    ren_try(frcs.gfx_cmd_pool, m_arena.create_command_pool({
                                   .name = fmt::format("Command pool {}", i),
                                   .queue_family = rhi::QueueFamily::Graphics,
                               }));
    if (m_renderer->is_queue_family_supported(rhi::QueueFamily::Compute)) {
      ren_try(frcs.async_cmd_pool,
              m_arena.create_command_pool({
                  .name = fmt::format("Command pool {}", i),
                  .queue_family = rhi::QueueFamily::Compute,
              }));
    }
    frcs.upload_allocator.init(*m_renderer, m_arena, 64 * MiB);
  }
  return {};
}

auto ScenePerFrameResources::reset(Renderer &renderer) -> Result<void, Error> {
  upload_allocator.reset();
  ren_try_to(renderer.reset_command_pool(gfx_cmd_pool));
  if (async_cmd_pool) {
    ren_try_to(renderer.reset_command_pool(async_cmd_pool));
  }
  descriptor_allocator.reset();
  end_semaphore = {};
  end_time = 0;
  return {};
}

auto Scene::next_frame() -> Result<void, Error> {
  ren_prof_zone("Scene::next_frame");

  m_frame_index++;
  m_frcs = &m_per_frame_resources[m_frame_index % NUM_FRAMES_IN_FLIGHT];

  {
    ren_prof_zone("Scene::wait_for_previous_frame");
    ren_prof_zone_text(
        fmt::format("{}", i64(m_frame_index - m_num_frames_in_flight)));
    if (m_renderer->try_get_semaphore(m_frcs->end_semaphore)) {
      ren_try_to(m_renderer->wait_for_semaphore(m_frcs->end_semaphore,
                                                m_frcs->end_time));
    }
  }

  ren_try_to(m_frcs->reset(*m_renderer));

  m_gfx_allocator.reset();
  CommandRecorder cmd;
  ren_try_to(cmd.begin(*m_renderer, m_frcs->gfx_cmd_pool));
  {
    auto _ = cmd.debug_region("begin-frame");
    cmd.memory_barrier(rhi::ALL_MEMORY_BARRIER);
  }
  ren_try(rhi::CommandBuffer cmd_buffer, cmd.end());
  ren_try_to(m_renderer->submit(rhi::QueueFamily::Graphics, {cmd_buffer}));

  if (m_data.settings.async_compute) {
    m_async_allocator.reset();
    std::swap(m_shared_allocators[0], m_shared_allocators[1]);
    m_shared_allocators[0].reset();
    CommandRecorder cmd;
    ren_try_to(cmd.begin(*m_renderer, m_frcs->async_cmd_pool));
    {
      auto _ = cmd.debug_region("begin-frame");
      cmd.memory_barrier(rhi::ALL_MEMORY_BARRIER);
    }
    ren_try(rhi::CommandBuffer cmd_buffer, cmd.end());
    ren_try_to(m_renderer->submit(rhi::QueueFamily::Compute, {cmd_buffer}));
  }

  return {};
}

auto Scene::create_mesh(const MeshCreateInfo &desc) -> expected<MeshId> {
  Vector<glsl::Position> positions;
  Vector<glsl::Normal> normals;
  Vector<glsl::Tangent> tangents;
  Vector<glsl::UV> uvs;
  Vector<glsl::Color> colors;
  Vector<glsl::Meshlet> meshlets;
  Vector<u32> meshlet_indices;
  Vector<u8> meshlet_triangles;

  Mesh mesh = mesh_process(MeshProcessingOptions{
      .positions = desc.positions,
      .normals = desc.normals,
      .tangents = desc.tangents,
      .uvs = desc.uvs,
      .colors = desc.colors,
      .indices = desc.indices,
      .enc_positions = &positions,
      .enc_normals = &normals,
      .enc_tangents = &tangents,
      .enc_uvs = &uvs,
      .enc_colors = &colors,
      .meshlets = &meshlets,
      .meshlet_indices = &meshlet_indices,
      .meshlet_triangles = &meshlet_triangles,
  });

  // Upload vertices

  auto upload_buffer = [&]<typename T>(const Vector<T> &data,
                                       Handle<Buffer> &buffer,
                                       DebugName name) -> Result<void, Error> {
    if (not data.empty()) {
      ren_try(BufferSlice<T> slice, m_arena.create_buffer<T>({
                                        .name = std::move(name),
                                        .heap = rhi::MemoryHeap::Default,
                                        .count = data.size(),
                                    }));
      buffer = slice.buffer;
      m_resource_uploader.stage_buffer(*m_renderer, m_frcs->upload_allocator,
                                       Span(data), slice);
    }
    return {};
  };

  u32 index = m_data.meshes.size();

  ren_try_to(upload_buffer(positions, mesh.positions,
                           fmt::format("Mesh {} positions", index)));
  ren_try_to(upload_buffer(normals, mesh.normals,
                           fmt::format("Mesh {} normals", index)));
  ren_try_to(upload_buffer(tangents, mesh.tangents,
                           fmt::format("Mesh {} tangents", index)));
  ren_try_to(upload_buffer(uvs, mesh.uvs, fmt::format("Mesh {} uvs", index)));
  ren_try_to(
      upload_buffer(colors, mesh.colors, fmt::format("Mesh {} colors", index)));

  // Find or allocate index pool

  u32 num_triangles = meshlet_triangles.size() / 3;

  ren_assert_msg(num_triangles * 3 <= glsl::INDEX_POOL_SIZE,
                 "Index pool overflow");

  if (m_data.index_pools.empty() or
      m_data.index_pools.back().num_free_indices < num_triangles * 3) {
    m_data.index_pools.emplace_back(create_index_pool(m_arena));
  }

  mesh.index_pool = m_data.index_pools.size() - 1;
  IndexPool &index_pool = m_data.index_pools.back();

  u32 base_triangle = glsl::INDEX_POOL_SIZE - index_pool.num_free_indices;
  for (glsl::Meshlet &meshlet : meshlets) {
    meshlet.base_triangle += base_triangle;
  }

  index_pool.num_free_indices -= num_triangles * 3;

  // Upload meshlets

  ren_try_to(upload_buffer(meshlets, mesh.meshlets,
                           fmt::format("Mesh {} meshlets", index)));
  ren_try_to(upload_buffer(meshlet_indices, mesh.meshlet_indices,
                           fmt::format("Mesh {} meshlet indices", index)));

  // Upload triangles

  m_resource_uploader.stage_buffer(
      *m_renderer, m_frcs->upload_allocator, Span(meshlet_triangles),
      m_renderer->get_buffer_slice<u8>(index_pool.indices)
          .slice(base_triangle, num_triangles * 3));

  Handle<Mesh> handle = m_data.meshes.insert(mesh);

  m_gpu_scene.update_meshes.push_back(handle);
  m_gpu_scene.mesh_update_data.push_back({
      .positions =
          m_renderer->get_buffer_device_ptr<glsl::Position>(mesh.positions),
      .normals = m_renderer->get_buffer_device_ptr<glsl::Normal>(mesh.normals),
      .tangents =
          m_renderer->try_get_buffer_device_ptr<glsl::Tangent>(mesh.tangents),
      .uvs = m_renderer->try_get_buffer_device_ptr<glsl::UV>(mesh.uvs),
      .colors = m_renderer->try_get_buffer_device_ptr<glsl::Color>(mesh.colors),
      .meshlets =
          m_renderer->get_buffer_device_ptr<glsl::Meshlet>(mesh.meshlets),
      .meshlet_indices =
          m_renderer->get_buffer_device_ptr<u32>(mesh.meshlet_indices),
      .bb = mesh.bb,
      .uv_bs = mesh.uv_bs,
      .index_pool = mesh.index_pool,
      .num_lods = u32(mesh.lods.size()),
  });
  std::ranges::copy(mesh.lods, m_gpu_scene.mesh_update_data.back().lods);

  return std::bit_cast<MeshId>(handle);
}

auto Scene::get_or_create_sampler(const SamplerCreateInfo &&create_info)
    -> Result<Handle<Sampler>, Error> {
  Handle<Sampler> &handle = m_sampler_cache[create_info];
  if (!handle) {
    ren_try(handle, m_arena.create_sampler(std::move(create_info)));
  }
  return handle;
}

auto Scene::get_or_create_texture(Handle<Image> image,
                                  const SamplerDesc &sampler_desc)
    -> Result<glsl::SampledTexture2D, Error> {
  ren_try(
      Handle<Sampler> sampler,
      get_or_create_sampler({
          .mag_filter = get_rhi_Filter(sampler_desc.mag_filter),
          .min_filter = get_rhi_Filter(sampler_desc.min_filter),
          .mipmap_mode = get_rhi_SamplerMipmapMode(sampler_desc.mipmap_filter),
          .address_mode_u = get_rhi_SamplerAddressMode(sampler_desc.wrap_u),
          .address_mode_v = get_rhi_SamplerAddressMode(sampler_desc.wrap_v),
          .anisotropy = 16.0f,
      }));
  ren_try(glsl::SampledTexture texture,
          m_descriptor_allocator.allocate_sampled_texture(
              *m_renderer, SrvDesc{m_images[image]}, sampler));
  return glsl::SampledTexture2D(texture);
}

auto Scene::create_image(const ImageCreateInfo &desc) -> expected<ImageId> {
  ren_try(
      auto texture,
      m_arena.create_texture({
          .format = desc.format,
          .usage = rhi::ImageUsage::ShaderResource |
                   rhi::ImageUsage::TransferSrc | rhi::ImageUsage::TransferDst,
          .width = desc.width,
          .height = desc.height,
          .num_mip_levels = get_mip_level_count(desc.width, desc.height),
      }));

  usize block_bits = TinyImageFormat_BitSizeOfBlock(desc.format);
  usize block_width = TinyImageFormat_WidthOfBlock(desc.format);
  usize block_height = TinyImageFormat_HeightOfBlock(desc.format);
  usize width_in_blocks = ceil_div(desc.width, block_width);
  usize height_in_blocks = ceil_div(desc.height, block_height);
  usize size = width_in_blocks * height_in_blocks * block_bits / 8;

  m_resource_uploader.stage_texture(*m_renderer, m_frcs->upload_allocator,
                                    Span((const std::byte *)desc.data, size),
                                    texture);
  Handle<Image> h = m_images.insert(texture);

  return std::bit_cast<ImageId>(h);
}

auto Scene::create_material(const MaterialCreateInfo &desc)
    -> expected<MaterialId> {
  auto get_sampled_texture_id =
      [&](const auto &texture) -> Result<glsl::SampledTexture2D, Error> {
    if (texture.image) {
      return get_or_create_texture(std::bit_cast<Handle<Image>>(texture.image),
                                   texture.sampler);
    }
    return {};
  };

  ren_try(auto base_color_texture,
          get_sampled_texture_id(desc.base_color_texture));
  ren_try(auto metallic_roughness_texture,
          get_sampled_texture_id(desc.metallic_roughness_texture));
  ren_try(auto normal_texture, get_sampled_texture_id(desc.normal_texture));

  Handle<Material> handle = m_data.materials.insert({
      .base_color = desc.base_color_factor,
      .base_color_texture = base_color_texture,
      .metallic = desc.metallic_factor,
      .roughness = desc.roughness_factor,
      .metallic_roughness_texture = metallic_roughness_texture,
      .normal_texture = normal_texture,
      .normal_scale = desc.normal_texture.scale,
  });
  m_gpu_scene.update_materials.push_back(handle);
  m_gpu_scene.material_update_data.push_back(m_data.materials[handle]);

  return std::bit_cast<MaterialId>(handle);
  ;
}

auto Scene::create_camera() -> expected<CameraId> {
  return std::bit_cast<CameraId>(m_data.cameras.emplace());
}

void Scene::destroy_camera(CameraId camera) {
  m_data.cameras.erase(std::bit_cast<Handle<Camera>>(camera));
}

auto Scene::get_camera(CameraId camera) -> Camera & {
  return m_data.cameras[std::bit_cast<Handle<Camera>>(camera)];
}

void Scene::set_camera(CameraId camera) {
  m_data.camera = std::bit_cast<Handle<Camera>>(camera);
}

void Scene::set_camera_perspective_projection(
    CameraId id, const CameraPerspectiveProjectionDesc &desc) {
  Camera &camera = get_camera(id);
  camera.proj = CameraProjection::Perspective;
  camera.persp_hfov = desc.hfov;
  camera.near = desc.near;
  camera.far = 0.0f;
}

void Scene::set_camera_orthographic_projection(
    CameraId id, const CameraOrthographicProjectionDesc &desc) {
  Camera &camera = get_camera(id);
  camera.proj = CameraProjection::Orthograpic;
  camera.ortho_width = desc.width;
  camera.near = desc.near;
  camera.far = desc.far;
}

void Scene::set_camera_transform(CameraId id, const CameraTransformDesc &desc) {
  Camera &camera = get_camera(id);
  camera.position = desc.position;
  camera.forward = desc.forward;
  camera.up = desc.up;
}

void Scene::set_camera_parameters(CameraId id,
                                  const CameraParameterDesc &desc) {
  get_camera(id).params = {
      .aperture = desc.aperture,
      .shutter_time = desc.shutter_time,
      .iso = desc.iso,
  };
}

void Scene::set_exposure(const ExposureDesc &desc) {
  m_data.exposure.mode = desc.mode;
  m_data.exposure.ec = desc.ec;
};

auto Scene::create_mesh_instances(
    std::span<const MeshInstanceCreateInfo> create_info,
    std::span<MeshInstanceId> out) -> expected<void> {
  ren_assert(out.size() >= create_info.size());
  for (usize i : range(create_info.size())) {
    auto mesh = std::bit_cast<Handle<Mesh>>(create_info[i].mesh);
    ren_assert(mesh);
    auto material = std::bit_cast<Handle<Material>>(create_info[i].material);
    ren_assert(material);
    Handle<MeshInstance> handle = m_data.mesh_instances.insert({
        .mesh = std::bit_cast<Handle<Mesh>>(create_info[i].mesh),
        .material = std::bit_cast<Handle<Material>>(create_info[i].material),
    });
    for (auto i : range(NUM_DRAW_SETS)) {
      DrawSet set = (DrawSet)(1 << i);
      add_to_draw_set(m_data, m_gpu_scene, m_pipelines, handle, set);
    }
    m_data.mesh_instance_transforms.insert(
        handle,
        glsl::make_decode_position_matrix(m_data.meshes[mesh].pos_enc_bb));
    m_gpu_scene.update_mesh_instances.push_back(handle);
    m_gpu_scene.mesh_instance_update_data.push_back({
        .mesh = mesh,
        .material = material,
    });
    out[i] = std::bit_cast<MeshInstanceId>(handle);
  }
  return {};
}

void Scene::destroy_mesh_instances(
    std::span<const MeshInstanceId> mesh_instances) {
  for (MeshInstanceId mesh_instance : mesh_instances) {
    auto handle = std::bit_cast<Handle<MeshInstance>>(mesh_instance);
    for (auto i : range(NUM_DRAW_SETS)) {
      DrawSet set = (DrawSet)(1 << i);
      remove_from_draw_set(m_data, m_gpu_scene, m_pipelines, handle, set);
    }
    m_data.mesh_instances.erase(handle);
  }
}

void Scene::set_mesh_instance_transforms(
    std::span<const MeshInstanceId> mesh_instances,
    std::span<const glm::mat4x3> matrices) {
  ren_assert(mesh_instances.size() == matrices.size());
  for (usize i : range(mesh_instances.size())) {
    auto h = std::bit_cast<Handle<MeshInstance>>(mesh_instances[i]);
    MeshInstance &mesh_instance = m_data.mesh_instances[h];
    const Mesh &mesh =
        m_data.meshes[std::bit_cast<Handle<Mesh>>(mesh_instance.mesh)];
    m_data.mesh_instance_transforms[h] =
        matrices[i] * glsl::make_decode_position_matrix(mesh.pos_enc_bb);
  }
}

auto Scene::create_directional_light(const DirectionalLightDesc &desc)
    -> expected<DirectionalLightId> {
  Handle<glsl::DirectionalLight> light = m_data.directional_lights.emplace();
  auto id = std::bit_cast<DirectionalLightId>(light);
  set_directional_light(id, desc);
  return id;
};

void Scene::destroy_directional_light(DirectionalLightId light) {
  m_data.directional_lights.erase(
      std::bit_cast<Handle<glsl::DirectionalLight>>(light));
}

void Scene::set_directional_light(DirectionalLightId light,
                                  const DirectionalLightDesc &desc) {
  auto handle = std::bit_cast<Handle<glsl::DirectionalLight>>(light);
  m_data.directional_lights[handle] = {
      .color = desc.color,
      .illuminance = desc.illuminance,
      .origin = glm::normalize(desc.origin),
  };
  m_gpu_scene.update_directional_lights.push_back(handle);
  m_gpu_scene.directional_light_update_data.push_back(
      m_data.directional_lights[handle]);
};

auto Scene::delay_input() -> expected<void> {
  if (is_amd_anti_lag_enabled()) {
    return m_renderer->amd_anti_lag_input(m_frame_index);
  }
  return {};
}

bool Scene::is_amd_anti_lag_available() {
  return m_renderer->is_feature_supported(RendererFeature::AmdAntiLag);
}

bool Scene::is_amd_anti_lag_enabled() {
  return is_amd_anti_lag_available() and m_data.settings.amd_anti_lag;
}

auto Scene::draw() -> expected<void> {
  ren_prof_zone("Scene::draw");

  ren_try_to(m_resource_uploader.upload(*m_renderer, m_frcs->gfx_cmd_pool));

  ren_try(RenderGraph render_graph, build_rg());

  ren_try_to(render_graph.execute({
      .gfx_cmd_pool = m_frcs->gfx_cmd_pool,
      .async_cmd_pool = m_frcs->async_cmd_pool,
      .frame_end_semaphore = &m_frcs->end_semaphore,
      .frame_end_time = &m_frcs->end_time,
  }));

  if (is_amd_anti_lag_enabled()) {
    ren_try_to(m_renderer->amd_anti_lag_present(m_frame_index));
  }

  auto present_qf = m_data.settings.present_from_compute
                        ? rhi::QueueFamily::Compute
                        : rhi::QueueFamily::Graphics;
  ren_try_to(m_swapchain->present(present_qf, m_frcs->present_semaphore));

  prof::mark_frame();

  return next_frame();
}

#if REN_IMGUI
void Scene::draw_imgui() {
  ren_prof_zone("Scene::draw_imgui");

  ren_ImGuiScope(m_imgui_context);
  if (ImGui::GetCurrentContext()) {
    if (ImGui::Begin("Scene renderer settings")) {
      SceneGraphicsSettings &settings = m_data.settings;

      ImGui::SeparatorText("Async compute");
      {
        ImGui::BeginDisabled(
            !m_renderer->is_queue_family_supported(rhi::QueueFamily::Compute));

        ImGui::Checkbox("Async compute", &settings.async_compute);

        ImGui::BeginDisabled(
            !m_swapchain->is_queue_family_supported(rhi::QueueFamily::Compute));
        ImGui::Checkbox("Present from compute", &settings.present_from_compute);
        ImGui::EndDisabled();

        ImGui::EndDisabled();
      }

      ImGui::SeparatorText("Latency");
      {
        ImGui::BeginDisabled(!is_amd_anti_lag_available());
        ImGui::Checkbox("AMD Anti-Lag", &settings.amd_anti_lag);
        ImGui::EndDisabled();
      }

      ImGui::SeparatorText("Instance culling");
      {
        ImGui::Checkbox("Frustum culling## Instance",
                        &settings.instance_frustum_culling);
        ImGui::Checkbox("Occlusion culling## Instance",
                        &settings.instance_occulusion_culling);
      }

      ImGui::SeparatorText("Level of detail");
      {
        ImGui::SliderInt("LOD bias", &settings.lod_bias,
                         -(glsl::MAX_NUM_LODS - 1), glsl::MAX_NUM_LODS - 1,
                         "%d");

        ImGui::Checkbox("LOD selection", &settings.lod_selection);

        ImGui::BeginDisabled(!settings.lod_selection);
        ImGui::SliderFloat("LOD pixels per triangle",
                           &settings.lod_triangle_pixels, 1.0f, 64.0f, "%.1f",
                           ImGuiSliderFlags_Logarithmic);
        ImGui::EndDisabled();
      }

      ImGui::SeparatorText("Meshlet culling");
      {
        ImGui::Checkbox("Cone culling", &settings.meshlet_cone_culling);
        ImGui::Checkbox("Frustum culling## Meshlet",
                        &settings.meshlet_frustum_culling);
        ImGui::BeginDisabled(!settings.instance_occulusion_culling);
        ImGui::Checkbox("Occlusion culling## Meshlet",
                        &settings.meshlet_occlusion_culling);
        ImGui::EndDisabled();
      }

      ImGui::SeparatorText("Opaque pass");
      {
        ImGui::Checkbox("Early Z", &settings.early_z);
      }

      ImGui::End();
    }
  }
}

void Scene::set_imgui_context(ImGuiContext *context) noexcept {
  m_imgui_context = context;
  if (!context) {
    return;
  }
  ren_ImGuiScope(m_imgui_context);
  ImGuiIO &io = ImGui::GetIO();
  io.BackendRendererName = "imgui_impl_ren";
  io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
  u8 *data;
  i32 width, height;
  io.Fonts->GetTexDataAsRGBA32(&data, &width, &height);
  ren::ImageId image =
      create_image({
                       .width = u32(width),
                       .height = u32(height),
                       .format = TinyImageFormat_R8G8B8A8_UNORM,
                       .data = data,
                   })
          .value();
  SamplerDesc desc = {
      .mag_filter = Filter::Linear,
      .min_filter = Filter::Linear,
      .mipmap_filter = Filter::Linear,
      .wrap_u = WrappingMode::Repeat,
      .wrap_v = WrappingMode::Repeat,
  };
  glsl::SampledTexture2D texture =
      get_or_create_texture(std::bit_cast<Handle<Image>>(image), desc).value();
  // NOTE: texture from old context is leaked. Don't really care since context
  // will probably be set only once
  io.Fonts->SetTexID((ImTextureID)(uintptr_t)texture);
}
#endif

auto Scene::build_rg() -> Result<RenderGraph, Error> {
  ren_prof_zone("Scene::build_rg");

  bool dirty = false;
  auto set_if_changed =
      [&]<typename T>(T &config_value,
                      const std::convertible_to<T> auto &new_value) {
        if (config_value != new_value) {
          config_value = new_value;
          dirty = true;
        }
      };

  set_if_changed(m_pass_cfg.async_compute, m_data.settings.async_compute);

  set_if_changed(m_pass_cfg.exposure, m_data.exposure.mode);

  set_if_changed(m_pass_cfg.viewport, m_swapchain->get_size());

  set_if_changed(m_pass_cfg.backbuffer_usage, m_swapchain->get_usage());

  if (dirty) {
    m_rgp->reset();
    m_rgp->set_async_compute_enabled(m_pass_cfg.async_compute);
    m_pass_rcs = {};
    m_pass_rcs.backbuffer = m_rgp->create_texture("backbuffer");
    m_pass_rcs.sdr = m_pass_rcs.backbuffer;
  }

  RgBuilder rgb(*m_rgp, *m_renderer, m_frcs->descriptor_allocator);

  PassCommonConfig cfg = {
      .rgp = m_rgp.get(),
      .rgb = &rgb,
      .allocator = &m_frcs->upload_allocator,
      .pipelines = &m_pipelines,
      .samplers = &m_samplers,
      .scene = &m_data,
      .rcs = &m_pass_rcs,
      .swapchain = m_swapchain,
  };

  RgGpuScene rg_gpu_scene = rg_import_gpu_scene(rgb, m_gpu_scene);
  setup_gpu_scene_update_pass(cfg, GpuSceneUpdatePassConfig{
                                       .gpu_scene = &m_gpu_scene,
                                       .rg_gpu_scene = &rg_gpu_scene,
                                   });

  RgTextureId exposure;
  setup_exposure_pass(cfg, ExposurePassConfig{
                               .exposure = &exposure,
                           });

  RgTextureId depth_buffer;
  RgTextureId hdr;
  setup_opaque_passes(cfg, OpaquePassesConfig{
                               .gpu_scene = &m_gpu_scene,
                               .rg_gpu_scene = &rg_gpu_scene,
                               .exposure = exposure,
                               .depth_buffer = &depth_buffer,
                               .hdr = &hdr,
                           });

  if (!cfg.rcs->acquire_semaphore) {
    cfg.rcs->acquire_semaphore = m_rgp->create_semaphore("acquire-semaphore");
  }

  rhi::ImageUsageFlags swap_chain_usage = rhi::ImageUsage::UnorderedAccess;

  RgTextureId sdr;
  setup_post_processing_passes(cfg, PostProcessingPassesConfig{
                                        .hdr = hdr,
                                        .exposure = exposure,
                                        .sdr = &sdr,
                                    });
#if REN_IMGUI
  if (m_imgui_context) {
    swap_chain_usage |= rhi::ImageUsage::RenderTarget;
    setup_imgui_pass(cfg, ImGuiPassConfig{
                              .ctx = m_imgui_context,
                              .sdr = &sdr,
                          });
  }
#endif

  m_swapchain->set_usage(swap_chain_usage);

  setup_present_pass(cfg, PresentPassConfig{
                              .src = sdr,
                              .acquire_semaphore = m_frcs->acquire_semaphore,
                              .present_semaphore = m_frcs->present_semaphore,
                              .swapchain = m_swapchain,
                          });

  return rgb.build({
      .gfx_allocator = &m_gfx_allocator,
      .async_allocator = &m_async_allocator,
      .shared_allocator = &m_shared_allocators[0],
      .upload_allocator = &m_frcs->upload_allocator,
  });
}

} // namespace ren
