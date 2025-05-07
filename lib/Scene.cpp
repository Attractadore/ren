#include "Scene.hpp"
#include "CommandRecorder.hpp"
#include "Formats.hpp"
#include "ImGuiConfig.hpp"
#include "Swapchain.hpp"
#include "core/Span.hpp"
#include "core/Views.hpp"
#include "glsl/Random.h"
#include "glsl/Transforms.h"
#include "passes/Exposure.hpp"
#include "passes/GpuSceneUpdate.hpp"
#include "passes/HiZ.hpp"
#include "passes/ImGui.hpp"
#include "passes/MeshPass.hpp"
#include "passes/Opaque.hpp"
#include "passes/PostProcessing.hpp"
#include "passes/Present.hpp"
#include "passes/Skybox.hpp"

#include "Ssao.comp.hpp"
#include "SsaoBlur.comp.hpp"
#include "SsaoHiZ.comp.hpp"

#include <fmt/format.h>
#include <ktx.h>
#include <tracy/Tracy.hpp>

namespace ren {

namespace {
constexpr u8 SO_LUT_KTX2[] = {
#include "../assets/so-lut.ktx2.inc"
};
} // namespace

Scene::Scene(Renderer &renderer, Swapchain &swapchain)
    : m_arena(renderer)

{
  m_renderer = &renderer;
  m_swapchain = &swapchain;

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

  m_descriptor_allocator.allocate_sampler(
      *m_renderer, rhi::SAMPLER_NEAREST_CLAMP, glsl::SAMPLER_NEAREST_CLAMP);

  m_descriptor_allocator.allocate_sampler(
      *m_renderer, rhi::SAMPLER_LINEAR_MIP_NEAREST_CLAMP,
      glsl::SAMPLER_LINEAR_MIP_NEAREST_CLAMP);

  next_frame().value();

  Handle<Texture> so_lut =
      create_texture(SO_LUT_KTX2, sizeof(SO_LUT_KTX2)).value();
  m_data.so_lut =
      m_descriptor_allocator
          .allocate_sampled_texture<glsl::SampledTexture3D>(
              *m_renderer, SrvDesc{so_lut},
              {
                  .mag_filter = rhi::Filter::Linear,
                  .min_filter = rhi::Filter::Linear,
                  .mipmap_mode = rhi::SamplerMipmapMode::Nearest,
                  .address_mode_u = rhi::SamplerAddressMode::ClampToEdge,
                  .address_mode_v = rhi::SamplerAddressMode::ClampToEdge,
                  .address_mode_w = rhi::SamplerAddressMode::ClampToEdge,
              })
          .value();
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
    ren_try_to(frcs.descriptor_allocator.init(m_descriptor_allocator));
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
  ZoneScoped;

  m_frame_index++;
  m_frcs = &m_per_frame_resources[m_frame_index % NUM_FRAMES_IN_FLIGHT];

  {
    ZoneScopedN("Scene::wait_for_previous_frame");
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

auto Scene::create_mesh(std::span<const std::byte> blob) -> expected<MeshId> {
  if (blob.size() < sizeof(MeshPackageHeader)) {
    return std::unexpected(Error::InvalidFormat);
  }

  const auto &header = *(const MeshPackageHeader *)blob.data();
  if (header.magic != MESH_PACKAGE_MAGIC) {
    return std::unexpected(Error::InvalidFormat);
  }
  if (header.version != MESH_PACKAGE_VERSION) {
    return std::unexpected(Error::InvalidVersion);
  }

  Span positions = {
      (const glsl::Position *)&blob[header.positions_offset],
      header.num_vertices,
  };

  Span normals = {
      (const glsl::Normal *)&blob[header.normals_offset],
      header.num_vertices,
  };

  Span tangents = {
      (const glsl::Tangent *)&blob[header.tangents_offset],
      header.tangents_offset ? header.num_vertices : 0,
  };

  Span uvs = {
      (const glsl::UV *)&blob[header.uvs_offset],
      header.uvs_offset ? header.num_vertices : 0,
  };

  Span colors = {
      (const glsl::Color *)&blob[header.colors_offset],
      header.colors_offset ? header.num_vertices : 0,
  };

  Span indices = {
      (const u32 *)&blob[header.indices_offset],
      header.num_indices,
  };

  // Create a copy because we need to patch base triangle indices.
  Vector<glsl::Meshlet> meshlets = Span{
      (const glsl::Meshlet *)&blob[header.meshlets_offset],
      header.num_meshlets,
  };

  Span triangles = {
      (const u8 *)&blob[header.triangles_offset],
      header.num_triangles * 3,
  };

  Mesh mesh = {
      .bb = header.bb,
      .pos_enc_bb = header.pos_enc_bb,
      .uv_bs = header.uv_bs,
      .lods = {&header.lods[0], &header.lods[header.num_lods]},
  };

  // Upload vertices

  auto upload_buffer = [&]<typename T>(Span<const T> data,
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

  ren_assert_msg(header.num_triangles * 3 <= glsl::INDEX_POOL_SIZE,
                 "Index pool overflow");

  if (m_data.index_pools.empty() or
      m_data.index_pools.back().num_free_indices < header.num_triangles * 3) {
    m_data.index_pools.emplace_back(create_index_pool(m_arena));
  }

  mesh.index_pool = m_data.index_pools.size() - 1;
  IndexPool &index_pool = m_data.index_pools.back();

  u32 base_triangle = glsl::INDEX_POOL_SIZE - index_pool.num_free_indices;
  for (glsl::Meshlet &meshlet : meshlets) {
    meshlet.base_triangle += base_triangle;
  }

  index_pool.num_free_indices -= header.num_triangles * 3;

  ren_try_to(upload_buffer(indices, mesh.indices,
                           fmt::format("Mesh {} indices", index)));

  // Upload meshlets

  ren_try_to(upload_buffer(Span<const glsl::Meshlet>(meshlets), mesh.meshlets,
                           fmt::format("Mesh {} meshlets", index)));

  // Upload triangles

  m_resource_uploader.stage_buffer(
      *m_renderer, m_frcs->upload_allocator, triangles,
      m_renderer->get_buffer_slice<u8>(index_pool.indices)
          .slice(base_triangle, header.num_triangles * 3));

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
      .meshlet_indices = m_renderer->get_buffer_device_ptr<u32>(mesh.indices),
      .bb = mesh.bb,
      .uv_bs = mesh.uv_bs,
      .index_pool = mesh.index_pool,
      .num_lods = u32(mesh.lods.size()),
  });
  std::ranges::copy(mesh.lods, m_gpu_scene.mesh_update_data.back().lods);

  return std::bit_cast<MeshId>(handle);
}

auto Scene::get_or_create_texture(Handle<Image> image,
                                  const SamplerDesc &sampler_desc)
    -> Result<glsl::SampledTexture2D, Error> {
  return m_descriptor_allocator
      .allocate_sampled_texture<glsl::SampledTexture2D>(
          *m_renderer, SrvDesc{m_images[image]},
          {
              .mag_filter = get_rhi_Filter(sampler_desc.mag_filter),
              .min_filter = get_rhi_Filter(sampler_desc.min_filter),
              .mipmap_mode =
                  get_rhi_SamplerMipmapMode(sampler_desc.mipmap_filter),
              .address_mode_u = get_rhi_SamplerAddressMode(sampler_desc.wrap_u),
              .address_mode_v = get_rhi_SamplerAddressMode(sampler_desc.wrap_v),
              .max_anisotropy = 16.0f,
          });
}

auto Scene::create_image(std::span<const std::byte> blob) -> expected<ImageId> {
  ren_try(auto texture, create_texture(blob.data(), blob.size()));
  Handle<Image> image = m_images.insert(texture);
  return std::bit_cast<ImageId>(image);
}

auto Scene::create_texture(const void *blob, usize size)
    -> expected<Handle<Texture>> {
  ktx_error_code_e err = KTX_SUCCESS;
  ktxTexture2 *ktx_texture2 = nullptr;
  err = ktxTexture2_CreateFromMemory((const u8 *)blob, size, 0, &ktx_texture2);
  if (err) {
    return Failure(Error::Unknown);
  }
  auto res = m_resource_uploader.create_texture(
      m_arena, m_frcs->upload_allocator, ktx_texture2);
  ktxTexture_Destroy(ktxTexture(ktx_texture2));
  return res;
}

auto Scene::create_material(const MaterialCreateInfo &info)
    -> expected<MaterialId> {
  auto get_descriptor =
      [&](const auto &texture) -> Result<glsl::SampledTexture2D, Error> {
    if (texture.image) {
      return get_or_create_texture(std::bit_cast<Handle<Image>>(texture.image),
                                   texture.sampler);
    }
    return {};
  };

  ren_try(auto base_color_texture, get_descriptor(info.base_color_texture));
  ren_try(auto orm_texture, get_descriptor(info.orm_texture));
  ren_try(auto normal_texture, get_descriptor(info.normal_texture));

  Handle<Material> handle = m_data.materials.insert({
      .base_color = info.base_color_factor,
      .base_color_texture = base_color_texture,
      .occlusion_strength = info.orm_texture.strength,
      .roughness = info.roughness_factor,
      .metallic = info.metallic_factor,
      .orm_texture = orm_texture,
      .normal_scale = info.normal_texture.scale,
      .normal_texture = normal_texture,
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

auto Scene::set_environment_map(ImageId image) -> expected<void> {
  if (!image) {
    m_data.env_map = {};
    return {};
  }
  Handle<Texture> texture = m_images[std::bit_cast<Handle<Image>>(image)];
  ren_try(
      m_data.env_map,
      m_descriptor_allocator.allocate_sampled_texture<glsl::SampledTextureCube>(
          *m_renderer, SrvDesc{texture},
          {
              .mag_filter = rhi::Filter::Linear,
              .min_filter = rhi::Filter::Linear,
              .mipmap_mode = rhi::SamplerMipmapMode::Linear,
          }));
  return {};
}

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
  ZoneScoped;

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

  FrameMark;

  return next_frame();
}

#if REN_IMGUI
void Scene::draw_imgui() {
  ZoneScoped;

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

      ImGui::SeparatorText("SSAO");
      {
        ImGui::Checkbox("SSAO", &settings.ssao);

        ImGui::BeginDisabled(!settings.ssao);
        ImGui::SliderInt("Sample count", &settings.ssao_num_samples, 1, 64,
                         "%d", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Radius", &settings.ssao_radius, 0.001f, 1.0f,
                           "%.3f", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("LOD bias## SSAO", &settings.ssao_lod_bias, -1.0f,
                           4.0f);
        ImGui::Checkbox("Full resolution## SSAO", &settings.ssao_full_res);
        ImGui::EndDisabled();
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
  i32 width, height, bpp;
  io.Fonts->GetTexDataAsRGBA32(&data, &width, &height, &bpp);
  Handle<Texture> texture = m_arena
                                .create_texture({
                                    .name = "ImGui font atlas",
                                    .format = TinyImageFormat_R8G8B8A8_UNORM,
                                    .usage = rhi::ImageUsage::ShaderResource |
                                             rhi::ImageUsage::TransferDst,
                                    .width = (u32)width,
                                    .height = (u32)height,
                                })
                                .value();
  m_resource_uploader.stage_texture(
      m_frcs->upload_allocator,
      Span((const std::byte *)data, width * height * bpp), texture);
  auto descriptor =
      m_descriptor_allocator
          .allocate_sampled_texture<glsl::SampledTexture2D>(
              *m_renderer, SrvDesc{texture},
              {
                  .mag_filter = rhi::Filter::Linear,
                  .min_filter = rhi::Filter::Linear,
                  .mipmap_mode = rhi::SamplerMipmapMode::Nearest,
                  .address_mode_u = rhi::SamplerAddressMode::Repeat,
                  .address_mode_v = rhi::SamplerAddressMode::Repeat,
              })
          .value();
  // FIXME: font texture is leaked.
  io.Fonts->SetTexID((ImTextureID)(uintptr_t)descriptor);
}
#endif

auto Scene::build_rg() -> Result<RenderGraph, Error> {
  ZoneScoped;

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

  set_if_changed(m_pass_cfg.ssao, m_data.settings.ssao);
  set_if_changed(m_pass_cfg.ssao_half_res, m_data.settings.ssao_full_res);

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

  glm::uvec2 viewport = m_swapchain->get_size();

  if (!m_pass_rcs.depth_buffer) {
    m_pass_rcs.depth_buffer = m_rgp->create_texture({
        .name = "depth-buffer",
        .format = DEPTH_FORMAT,
        .width = viewport.x,
        .height = viewport.y,
    });
  }
  RgTextureId depth_buffer = m_pass_rcs.depth_buffer;
  RgTextureId hi_z;

  auto occlusion_culling_mode = OcclusionCullingMode::Disabled;
  if (m_data.settings.instance_occulusion_culling) {
    occlusion_culling_mode = OcclusionCullingMode::FirstPhase;
  }

  setup_early_z_pass(cfg, EarlyZPassConfig{
                              .gpu_scene = &m_gpu_scene,
                              .rg_gpu_scene = &rg_gpu_scene,
                              .occlusion_culling_mode = occlusion_culling_mode,
                              .depth_buffer = &depth_buffer,
                          });
  if (occlusion_culling_mode == OcclusionCullingMode::FirstPhase) {
    setup_hi_z_pass(cfg,
                    HiZPassConfig{.depth_buffer = depth_buffer, .hi_z = &hi_z});
    setup_early_z_pass(
        cfg, EarlyZPassConfig{
                 .gpu_scene = &m_gpu_scene,
                 .rg_gpu_scene = &rg_gpu_scene,
                 .occlusion_culling_mode = OcclusionCullingMode::SecondPhase,
                 .depth_buffer = &depth_buffer,
                 .hi_z = hi_z,
             });
    occlusion_culling_mode = OcclusionCullingMode::ThirdPhase;
  }

  RgTextureId ssao_blurred;
  RgTextureId ssao_depth = m_pass_rcs.ssao_depth;
  if (m_data.settings.ssao) {
    glm::uvec2 ssao_hi_z_size = {std::bit_floor(viewport.x),
                                 std::bit_floor(viewport.y)};
    // Don't need the full mip chain since after a certain stage they become
    // small enough to completely fit in cache but are much less detailed.
    i32 num_ssao_hi_z_mips =
        get_mip_chain_length(std::min(ssao_hi_z_size.x, ssao_hi_z_size.y));
    num_ssao_hi_z_mips =
        std::max<i32>(num_ssao_hi_z_mips - get_mip_chain_length(32) + 1, 1);

    if (!m_pass_rcs.ssao_hi_z) {
      m_pass_rcs.ssao_hi_z = m_rgp->create_texture({
          .name = "ssao-hi-z",
          .format = TinyImageFormat_R16_SFLOAT,
          .width = ssao_hi_z_size.x,
          .height = ssao_hi_z_size.y,
          .num_mips = (u32)num_ssao_hi_z_mips,
      });
    }
    RgTextureId ssao_hi_z = m_pass_rcs.ssao_hi_z;

    const Camera &camera = m_data.get_camera();
    glm::mat4 proj = get_projection_matrix(camera, viewport);

    {
      auto pass = rgb.create_pass({.name = "ssao-hi-z"});
      auto spd_counter = rgb.create_buffer<u32>({.init = 0});
      RgLinearHiZArgs args = {
          .spd_counter =
              pass.write_buffer("ssao-hi-z-spd-counter", &spd_counter),
          .num_mips = (u32)num_ssao_hi_z_mips,
          .dsts = pass.write_texture("ssao-hi-z", &ssao_hi_z),
          .src = pass.read_texture(
              depth_buffer,
              {
                  .mag_filter = rhi::Filter::Linear,
                  .min_filter = rhi::Filter::Linear,
                  .mipmap_mode = rhi::SamplerMipmapMode::Nearest,
                  .address_mode_u = rhi::SamplerAddressMode::ClampToEdge,
                  .address_mode_v = rhi::SamplerAddressMode::ClampToEdge,
              }),
          .znear = camera.near,
      };
      pass.dispatch_grid_2d(
          m_pipelines.ssao_hi_z, args, ssao_hi_z_size,
          {glsl::SPD_THREAD_ELEMS_X, glsl::SPD_THREAD_ELEMS_Y});
    }

    glm::uvec2 ssao_size =
        m_data.settings.ssao_full_res ? viewport : viewport / 2u;

    if (!m_pass_rcs.ssao) {
      m_pass_rcs.ssao = m_rgp->create_texture({
          .name = "ssao",
          .format = TinyImageFormat_R16_UNORM,
          .width = ssao_size.x,
          .height = ssao_size.y,
      });
    }
    if (!m_data.settings.ssao_full_res and !m_pass_rcs.ssao_depth) {
      m_pass_rcs.ssao_depth = m_rgp->create_texture({
          .name = "ssao-depth",
          .format = TinyImageFormat_R16_SFLOAT,
          .width = ssao_size.x,
          .height = ssao_size.y,
      });
    }

    RgTextureId ssao = m_pass_rcs.ssao;
    ssao_depth = m_pass_rcs.ssao_depth;
    {
      auto pass = rgb.create_pass({.name = "ssao"});

      auto noise_lut = m_frcs->upload_allocator.allocate<float>(
          glsl::SSAO_HILBERT_CURVE_SIZE * glsl::SSAO_HILBERT_CURVE_SIZE);

      for (u32 y : range(glsl::SSAO_HILBERT_CURVE_SIZE)) {
        for (u32 x : range(glsl::SSAO_HILBERT_CURVE_SIZE)) {
          u32 h = glsl::hilbert_from_2d(glsl::SSAO_HILBERT_CURVE_SIZE, x, y);
          noise_lut.host_ptr[y * glsl::SSAO_HILBERT_CURVE_SIZE + x] =
              glsl::r1_seq(h);
        }
      }

      RgSsaoArgs args = {
          .noise_lut = noise_lut.device_ptr,
          .depth = pass.read_texture(depth_buffer, rhi::SAMPLER_NEAREST_CLAMP),
          .hi_z = pass.read_texture(ssao_hi_z, rhi::SAMPLER_NEAREST_CLAMP),
          .ssao = pass.write_texture("ssao", &ssao),
          .ssao_depth = ssao_depth
                            ? pass.write_texture("ssao-depth", &ssao_depth)
                            : RgTextureToken(),
          .num_samples = (u32)m_data.settings.ssao_num_samples,
          .p00 = proj[0][0],
          .p11 = proj[1][1],
          .znear = camera.near,
          .rcp_p00 = 1.0f / proj[0][0],
          .rcp_p11 = 1.0f / proj[1][1],
          .radius = m_data.settings.ssao_radius,
          .lod_bias = m_data.settings.ssao_lod_bias,
      };
      pass.dispatch_grid_2d(m_pipelines.ssao, args, ssao_size);
    }

    if (!m_pass_rcs.ssao_blurred) {
      m_pass_rcs.ssao_blurred = m_rgp->create_texture({
          .name = "ssao-blurred",
          .format = TinyImageFormat_R16_UNORM,
          .width = ssao_size.x,
          .height = ssao_size.y,
      });
    }
    ssao_blurred = m_pass_rcs.ssao_blurred;
    {
      auto pass = rgb.create_pass({.name = "ssao-blur"});
      RgSsaoBlurArgs args = {
          .depth =
              !ssao_depth ? pass.read_texture(depth_buffer) : RgTextureToken(),
          .ssao = pass.read_texture(ssao),
          .ssao_depth = pass.try_read_texture(ssao_depth),
          .ssao_blurred = pass.write_texture("ssao-blurred", &ssao_blurred),
          .znear = camera.near,
          .radius = m_data.settings.ssao_radius,
      };
      pass.dispatch_grid_2d(
          m_pipelines.ssao_blur, args, ssao_size,
          {glsl::SSAO_BLUR_THREAD_ITEMS_X, glsl::SSAO_BLUR_THREAD_ITEMS_Y});
    }
  }

  if (!m_pass_rcs.hdr) {
    m_pass_rcs.hdr = m_rgp->create_texture({
        .name = "hdr",
        .format = HDR_FORMAT,
        .width = viewport.x,
        .height = viewport.y,
    });
  }
  RgTextureId hdr = m_pass_rcs.hdr;

  setup_opaque_pass(cfg, OpaquePassConfig{
                             .gpu_scene = &m_gpu_scene,
                             .rg_gpu_scene = &rg_gpu_scene,
                             .occlusion_culling_mode = occlusion_culling_mode,
                             .hdr = &hdr,
                             .depth_buffer = &depth_buffer,
                             .hi_z = hi_z,
                             .ssao = ssao_blurred,
                             .ssao_depth = ssao_depth,
                             .exposure = exposure,
                         });

  setup_skybox_pass(cfg, SkyboxPassConfig{
                             .exposure = exposure,
                             .hdr = &hdr,
                             .depth_buffer = depth_buffer,
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

auto create_scene(IRenderer &renderer, ISwapchain &swapchain)
    -> expected<std::unique_ptr<IScene>> {
  return std::make_unique<Scene>(static_cast<Renderer &>(renderer),
                                 static_cast<Swapchain &>(swapchain));
}

} // namespace ren
