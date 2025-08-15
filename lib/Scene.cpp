#include "Scene.hpp"
#include "CommandRecorder.hpp"
#include "Formats.hpp"
#include "ImGuiConfig.hpp"
#include "SwapChain.hpp"
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
#include "ren/ren.hpp"

#include "Ssao.comp.hpp"
#include "SsaoFilter.comp.hpp"
#include "SsaoHiZ.comp.hpp"

#include <fmt/format.h>
#include <ktx.h>
#include <tracy/Tracy.hpp>

namespace ren_export {

namespace {

auto init_scene_internal_data(Renderer *renderer,
                              DescriptorAllocator &descriptor_allocator)
    -> Result<std::unique_ptr<SceneInternalData>, Error> {
  auto sid = std::make_unique<SceneInternalData>();
  sid->m_arena.init(renderer);
  ren_try(sid->m_pipelines, load_pipelines(sid->m_arena));

  sid->m_gfx_allocator.init(*renderer, sid->m_arena, 256 * MiB);
  if (renderer->is_queue_family_supported(rhi::QueueFamily::Compute)) {
    sid->m_async_allocator.init(*renderer, sid->m_arena, 16 * MiB);
    for (DeviceBumpAllocator &allocator : sid->m_shared_allocators) {
      allocator.init(*renderer, sid->m_arena, 16 * MiB);
    }
  }

  for (auto i : range(NUM_FRAMES_IN_FLIGHT)) {
    ScenePerFrameResources &frcs = sid->m_per_frame_resources[i];
    ren_try(frcs.acquire_semaphore,
            sid->m_arena.create_semaphore({
                .name = fmt::format("Acquire semaphore {}", i),
                .type = rhi::SemaphoreType::Binary,
            }));
    ren_try(frcs.gfx_cmd_pool, sid->m_arena.create_command_pool({
                                   .name = fmt::format("Command pool {}", i),
                                   .queue_family = rhi::QueueFamily::Graphics,
                               }));
    if (renderer->is_queue_family_supported(rhi::QueueFamily::Compute)) {
      ren_try(frcs.async_cmd_pool,
              sid->m_arena.create_command_pool({
                  .name = fmt::format("Command pool {}", i),
                  .queue_family = rhi::QueueFamily::Compute,
              }));
    }
    frcs.upload_allocator.init(*renderer, sid->m_arena, 64 * MiB);
    ren_try_to(frcs.descriptor_allocator.init(descriptor_allocator));
  }

  sid->m_rgp.init(renderer);

  return sid;
}

} // namespace

auto create_scene(Renderer *renderer, SwapChain *swap_chain)
    -> expected<Scene *> {
  auto *scene = new Scene{
      .m_renderer = renderer,
      .m_swap_chain = swap_chain,
  };
  scene->m_arena.init(renderer);
  scene->m_gpu_scene = init_gpu_scene(scene->m_arena);

  scene->m_descriptor_allocator.allocate_sampler(
      *renderer, rhi::SAMPLER_NEAREST_CLAMP, glsl::SAMPLER_NEAREST_CLAMP);
  scene->m_descriptor_allocator.allocate_sampler(
      *renderer, rhi::SAMPLER_LINEAR_MIP_NEAREST_CLAMP,
      glsl::SAMPLER_LINEAR_MIP_NEAREST_CLAMP);

  scene->m_data.settings.async_compute =
      renderer->is_queue_family_supported(rhi::QueueFamily::Compute);
  scene->m_data.settings.present_from_compute =
      swap_chain->is_queue_family_supported(rhi::QueueFamily::Compute);

  scene->m_data.settings.amd_anti_lag =
      renderer->is_feature_supported(RendererFeature::AmdAntiLag);

  ren_try(scene->m_sid,
          init_scene_internal_data(renderer, scene->m_descriptor_allocator));

  ren_try_to(scene->next_frame());

  return scene;
}

void destroy_scene(Scene *scene) { delete scene; }

auto create_mesh(Scene *scene, std::span<const std::byte> blob)
    -> expected<MeshId> {
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
      .scale = header.scale,
      .uv_bs = header.uv_bs,
      .lods = {&header.lods[0], &header.lods[header.num_lods]},
  };

  // Upload vertices

  Renderer *renderer = scene->m_renderer;

  auto upload_buffer = [&]<typename T>(Span<const T> data,
                                       Handle<Buffer> &buffer,
                                       DebugName name) -> Result<void, Error> {
    if (not data.empty()) {
      ren_try(BufferSlice<T> slice, scene->m_arena.create_buffer<T>({
                                        .name = std::move(name),
                                        .heap = rhi::MemoryHeap::Default,
                                        .count = data.size(),
                                    }));
      buffer = slice.buffer;
      scene->m_resource_uploader.stage_buffer(
          *renderer, scene->m_frcs->upload_allocator, Span(data), slice);
    }
    return {};
  };

  u32 index = scene->m_data.meshes.size();

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

  if (scene->m_data.index_pools.empty() or
      scene->m_data.index_pools.back().num_free_indices <
          header.num_triangles * 3) {
    scene->m_data.index_pools.emplace_back(create_index_pool(scene->m_arena));
  }

  mesh.index_pool = scene->m_data.index_pools.size() - 1;
  IndexPool &index_pool = scene->m_data.index_pools.back();

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

  scene->m_resource_uploader.stage_buffer(
      *renderer, scene->m_frcs->upload_allocator, triangles,
      renderer->get_buffer_slice<u8>(index_pool.indices)
          .slice(base_triangle, header.num_triangles * 3));

  Handle<Mesh> handle = scene->m_data.meshes.insert(mesh);

  scene->m_gpu_scene.update_meshes.push_back(handle);
  scene->m_gpu_scene.mesh_update_data.push_back({
      .positions =
          renderer->get_buffer_device_ptr<glsl::Position>(mesh.positions),
      .normals = renderer->get_buffer_device_ptr<glsl::Normal>(mesh.normals),
      .tangents =
          renderer->try_get_buffer_device_ptr<glsl::Tangent>(mesh.tangents),
      .uvs = renderer->try_get_buffer_device_ptr<glsl::UV>(mesh.uvs),
      .colors = renderer->try_get_buffer_device_ptr<glsl::Color>(mesh.colors),
      .meshlets = renderer->get_buffer_device_ptr<glsl::Meshlet>(mesh.meshlets),
      .meshlet_indices = renderer->get_buffer_device_ptr<u32>(mesh.indices),
      .bb = mesh.bb,
      .uv_bs = mesh.uv_bs,
      .index_pool = mesh.index_pool,
      .num_lods = u32(mesh.lods.size()),
  });
  std::ranges::copy(mesh.lods, scene->m_gpu_scene.mesh_update_data.back().lods);

  return std::bit_cast<MeshId>(handle);
}

auto create_image(Scene *scene, std::span<const std::byte> blob)
    -> expected<ImageId> {
  ren_try(auto texture, scene->create_texture(blob.data(), blob.size()));
  Handle<Image> image = scene->m_images.insert(texture);
  return std::bit_cast<ImageId>(image);
}

auto create_material(Scene *scene, const MaterialCreateInfo &info)
    -> expected<MaterialId> {
  auto get_descriptor =
      [&](const auto &texture) -> Result<glsl::SampledTexture2D, Error> {
    if (texture.image) {
      return scene->get_or_create_texture(
          std::bit_cast<Handle<Image>>(texture.image), texture.sampler);
    }
    return {};
  };

  ren_try(auto base_color_texture, get_descriptor(info.base_color_texture));
  ren_try(auto orm_texture, get_descriptor(info.orm_texture));
  ren_try(auto normal_texture, get_descriptor(info.normal_texture));

  Handle<Material> handle = scene->m_data.materials.insert({
      .base_color = info.base_color_factor,
      .base_color_texture = base_color_texture,
      .occlusion_strength = info.orm_texture.strength,
      .roughness = info.roughness_factor,
      .metallic = info.metallic_factor,
      .orm_texture = orm_texture,
      .normal_scale = info.normal_texture.scale,
      .normal_texture = normal_texture,
  });
  scene->m_gpu_scene.update_materials.push_back(handle);
  scene->m_gpu_scene.material_update_data.push_back(
      scene->m_data.materials[handle]);

  return std::bit_cast<MaterialId>(handle);
  ;
}

auto create_camera(Scene *scene) -> expected<CameraId> {
  return std::bit_cast<CameraId>(scene->m_data.cameras.emplace());
}

void destroy_camera(Scene *scene, CameraId camera) {
  scene->m_data.cameras.erase(std::bit_cast<Handle<Camera>>(camera));
}

void set_camera(Scene *scene, CameraId camera) {
  scene->m_data.camera = std::bit_cast<Handle<Camera>>(camera);
}

void set_camera_perspective_projection(
    Scene *scene, CameraId id, const CameraPerspectiveProjectionDesc &desc) {
  Camera &camera = scene->get_camera(id);
  camera.proj = CameraProjection::Perspective;
  camera.persp_hfov = desc.hfov;
  camera.near = desc.near;
  camera.far = 0.0f;
}

void set_camera_orthographic_projection(
    Scene *scene, CameraId id, const CameraOrthographicProjectionDesc &desc) {
  Camera &camera = scene->get_camera(id);
  camera.proj = CameraProjection::Orthograpic;
  camera.ortho_width = desc.width;
  camera.near = desc.near;
  camera.far = desc.far;
}

void set_camera_transform(Scene *scene, CameraId id,
                          const CameraTransformDesc &desc) {
  Camera &camera = scene->get_camera(id);
  camera.position = desc.position;
  camera.forward = desc.forward;
  camera.up = desc.up;
}

void set_camera_parameters(Scene *scene, CameraId id,
                           const CameraParameterDesc &desc) {
  scene->get_camera(id).params = {
      .aperture = desc.aperture,
      .shutter_time = desc.shutter_time,
      .iso = desc.iso,
  };
}

void set_exposure(Scene *scene, const ExposureDesc &desc) {
  scene->m_data.exposure.mode = desc.mode;
  scene->m_data.exposure.ec = desc.ec;
};

auto create_mesh_instances(Scene *scene,
                           std::span<const MeshInstanceCreateInfo> create_info,
                           std::span<MeshInstanceId> out) -> expected<void> {
  ren_assert(out.size() >= create_info.size());
  for (usize i : range(create_info.size())) {
    auto mesh = std::bit_cast<Handle<Mesh>>(create_info[i].mesh);
    ren_assert(mesh);
    auto material = std::bit_cast<Handle<Material>>(create_info[i].material);
    ren_assert(material);
    Handle<MeshInstance> handle = scene->m_data.mesh_instances.insert({
        .mesh = std::bit_cast<Handle<Mesh>>(create_info[i].mesh),
        .material = std::bit_cast<Handle<Material>>(create_info[i].material),
    });
    for (auto i : range(NUM_DRAW_SETS)) {
      DrawSet set = (DrawSet)(1 << i);
      add_to_draw_set(scene->m_data, scene->m_gpu_scene, handle, set);
    }
    scene->m_data.mesh_instance_transforms.insert(
        handle,
        glsl::make_decode_position_matrix(scene->m_data.meshes[mesh].scale));
    scene->m_gpu_scene.update_mesh_instances.push_back(handle);
    scene->m_gpu_scene.mesh_instance_update_data.push_back({
        .mesh = mesh,
        .material = material,
    });
    out[i] = std::bit_cast<MeshInstanceId>(handle);
  }
  return {};
}

void destroy_mesh_instances(Scene *scene,
                            std::span<const MeshInstanceId> mesh_instances) {
  for (MeshInstanceId mesh_instance : mesh_instances) {
    auto handle = std::bit_cast<Handle<MeshInstance>>(mesh_instance);
    for (auto i : range(NUM_DRAW_SETS)) {
      DrawSet set = (DrawSet)(1 << i);
      remove_from_draw_set(scene->m_data, scene->m_gpu_scene, handle, set);
    }
    scene->m_data.mesh_instances.erase(handle);
  }
}

void set_mesh_instance_transforms(
    Scene *scene, std::span<const MeshInstanceId> mesh_instances,
    std::span<const glm::mat4x3> matrices) {
  ren_assert(mesh_instances.size() == matrices.size());
  for (usize i : range(mesh_instances.size())) {
    auto h = std::bit_cast<Handle<MeshInstance>>(mesh_instances[i]);
    MeshInstance &mesh_instance = scene->m_data.mesh_instances[h];
    const Mesh &mesh =
        scene->m_data.meshes[std::bit_cast<Handle<Mesh>>(mesh_instance.mesh)];
    scene->m_data.mesh_instance_transforms[h] =
        matrices[i] * glsl::make_decode_position_matrix(mesh.scale);
  }
}

auto create_directional_light(Scene *scene, const DirectionalLightDesc &desc)
    -> expected<DirectionalLightId> {
  Handle<glsl::DirectionalLight> light =
      scene->m_data.directional_lights.emplace();
  auto id = std::bit_cast<DirectionalLightId>(light);
  ren_export::set_directional_light(scene, id, desc);
  return id;
};

void destroy_directional_light(Scene *scene, DirectionalLightId light) {
  scene->m_data.directional_lights.erase(
      std::bit_cast<Handle<glsl::DirectionalLight>>(light));
}

void set_directional_light(Scene *scene, DirectionalLightId light,
                           const DirectionalLightDesc &desc) {
  auto handle = std::bit_cast<Handle<glsl::DirectionalLight>>(light);
  scene->m_data.directional_lights[handle] = {
      .color = desc.color,
      .illuminance = desc.illuminance,
      .origin = glm::normalize(desc.origin),
  };
  scene->m_gpu_scene.update_directional_lights.push_back(handle);
  scene->m_gpu_scene.directional_light_update_data.push_back(
      scene->m_data.directional_lights[handle]);
};

void set_environment_color(Scene *scene, const glm::vec3 &luminance) {
  scene->m_data.env_luminance = luminance;
}

auto set_environment_map(Scene *scene, ImageId image) -> expected<void> {
  if (!image) {
    scene->m_data.env_map = {};
    return {};
  }
  Handle<Texture> texture =
      scene->m_images[std::bit_cast<Handle<Image>>(image)];
  ren_try(scene->m_data.env_map,
          scene->m_descriptor_allocator
              .allocate_sampled_texture<glsl::SampledTextureCube>(
                  *scene->m_renderer, SrvDesc{texture},
                  {
                      .mag_filter = rhi::Filter::Linear,
                      .min_filter = rhi::Filter::Linear,
                      .mipmap_mode = rhi::SamplerMipmapMode::Linear,
                  }));
  return {};
}

auto delay_input(Scene *scene) -> expected<void> {
  if (scene->is_amd_anti_lag_enabled()) {
    return scene->m_renderer->amd_anti_lag_input(scene->m_frame_index);
  }
  return {};
}

auto draw(Scene *scene) -> expected<void> {
  ZoneScoped;

  Renderer *renderer = scene->m_renderer;
  auto *frcs = scene->m_frcs;

  ren_try_to(scene->m_resource_uploader.upload(*renderer,
                                               scene->m_frcs->gfx_cmd_pool));

  ren_try(RenderGraph render_graph, scene->build_rg());

  ren_try_to(render_graph.execute({
      .gfx_cmd_pool = frcs->gfx_cmd_pool,
      .async_cmd_pool = frcs->async_cmd_pool,
      .frame_end_semaphore = &frcs->end_semaphore,
      .frame_end_time = &frcs->end_time,
  }));

  if (scene->is_amd_anti_lag_enabled()) {
    ren_try_to(renderer->amd_anti_lag_present(scene->m_frame_index));
  }

  auto present_qf = scene->m_data.settings.present_from_compute
                        ? rhi::QueueFamily::Compute
                        : rhi::QueueFamily::Graphics;
  ren_try_to(scene->m_swap_chain->present(present_qf));

  FrameMark;

  return scene->next_frame();
}

#if REN_IMGUI

namespace imgui {

auto draw(Scene *scene) -> expected<void> {
  ZoneScoped;

  ren_ImGuiScope(scene->m_imgui_context);
  if (ImGui::GetCurrentContext()) {
    if (ImGui::Begin("Scene renderer settings")) {
      SceneGraphicsSettings &settings = scene->m_data.settings;

      ImGui::SeparatorText("Async compute");
      {
        ImGui::BeginDisabled(!scene->m_renderer->is_queue_family_supported(
            rhi::QueueFamily::Compute));

        ImGui::Checkbox("Async compute", &settings.async_compute);

        ImGui::BeginDisabled(!scene->m_swap_chain->is_queue_family_supported(
            rhi::QueueFamily::Compute));
        ImGui::Checkbox("Present from compute", &settings.present_from_compute);
        ImGui::EndDisabled();

        ImGui::EndDisabled();
      }

      ImGui::SeparatorText("Latency");
      {
        ImGui::BeginDisabled(!scene->is_amd_anti_lag_available());
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

  return {};
}

void set_context(Scene *scene, ImGuiContext *context) {
  scene->m_imgui_context = context;
  if (!context) {
    return;
  }
  ren_ImGuiScope(scene->m_imgui_context);
  ImGuiIO &io = ImGui::GetIO();
  io.BackendRendererName = "imgui_impl_ren";
  io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
  u8 *data;
  i32 width, height, bpp;
  io.Fonts->GetTexDataAsRGBA32(&data, &width, &height, &bpp);
  Handle<Texture> texture = scene->m_arena
                                .create_texture({
                                    .name = "ImGui font atlas",
                                    .format = TinyImageFormat_R8G8B8A8_UNORM,
                                    .usage = rhi::ImageUsage::ShaderResource |
                                             rhi::ImageUsage::TransferDst,
                                    .width = (u32)width,
                                    .height = (u32)height,
                                })
                                .value();
  scene->m_resource_uploader.stage_texture(
      scene->m_frcs->upload_allocator,
      Span((const std::byte *)data, width * height * bpp), texture);
  auto descriptor =
      scene->m_descriptor_allocator
          .allocate_sampled_texture<glsl::SampledTexture2D>(
              *scene->m_renderer, SrvDesc{texture},
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

auto get_context(Scene *scene) -> ImGuiContext * {
  return scene->m_imgui_context;
}

} // namespace imgui
#endif

} // namespace ren_export

namespace ren {

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
  m_frcs = &m_sid->m_per_frame_resources[m_frame_index % NUM_FRAMES_IN_FLIGHT];

  {
    ZoneScopedN("Scene::wait_for_previous_frame");
    if (m_renderer->try_get_semaphore(m_frcs->end_semaphore)) {
      ren_try_to(m_renderer->wait_for_semaphore(m_frcs->end_semaphore,
                                                m_frcs->end_time));
    }
  }

  ren_try_to(m_frcs->reset(*m_renderer));

  m_sid->m_gfx_allocator.reset();
  CommandRecorder cmd;
  ren_try_to(cmd.begin(*m_renderer, m_frcs->gfx_cmd_pool));
  {
    auto _ = cmd.debug_region("begin-frame");
    cmd.memory_barrier(rhi::ALL_MEMORY_BARRIER);
  }
  ren_try(rhi::CommandBuffer cmd_buffer, cmd.end());
  ren_try_to(m_renderer->submit(rhi::QueueFamily::Graphics, {cmd_buffer}));

  if (m_data.settings.async_compute) {
    m_sid->m_async_allocator.reset();
    std::swap(m_sid->m_shared_allocators[0], m_sid->m_shared_allocators[1]);
    m_sid->m_shared_allocators[0].reset();
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

auto Scene::get_camera(CameraId camera) -> Camera & {
  return m_data.cameras[std::bit_cast<Handle<Camera>>(camera)];
}

bool Scene::is_amd_anti_lag_available() {
  return m_renderer->is_feature_supported(RendererFeature::AmdAntiLag);
}

bool Scene::is_amd_anti_lag_enabled() {
  return is_amd_anti_lag_available() and m_data.settings.amd_anti_lag;
}

auto Scene::build_rg() -> Result<RenderGraph, Error> {
  ZoneScoped;

  auto &pass_cfg = m_sid->m_pass_cfg;
  PassPersistentResources &pass_rcs = m_sid->m_pass_rcs;
  RgPersistent &rgp = m_sid->m_rgp;

  bool dirty = false;
  auto set_if_changed =
      [&]<typename T>(T &config_value,
                      const std::convertible_to<T> auto &new_value) {
        if (config_value != new_value) {
          config_value = new_value;
          dirty = true;
        }
      };

  set_if_changed(pass_cfg.async_compute, m_data.settings.async_compute);

  set_if_changed(pass_cfg.exposure, m_data.exposure.mode);

  set_if_changed(pass_cfg.viewport, m_swap_chain->get_size());

  set_if_changed(pass_cfg.backbuffer_usage, m_swap_chain->get_usage());

  set_if_changed(pass_cfg.ssao, m_data.settings.ssao);
  set_if_changed(pass_cfg.ssao_half_res, m_data.settings.ssao_full_res);

  if (dirty) {
    rgp.reset();
    rgp.set_async_compute_enabled(pass_cfg.async_compute);
    pass_rcs = {};
    pass_rcs.backbuffer = rgp.create_texture("backbuffer");
    pass_rcs.sdr = pass_rcs.backbuffer;
  }

  RgBuilder rgb(rgp, *m_renderer, m_frcs->descriptor_allocator);

  PassCommonConfig cfg = {
      .rgp = &rgp,
      .rgb = &rgb,
      .allocator = &m_frcs->upload_allocator,
      .pipelines = &m_sid->m_pipelines,
      .scene = &m_data,
      .rcs = &pass_rcs,
      .viewport = m_swap_chain->get_size(),
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

  glm::uvec2 viewport = m_swap_chain->get_size();

  if (!pass_rcs.depth_buffer) {
    pass_rcs.depth_buffer = rgp.create_texture({
        .name = "depth-buffer",
        .format = DEPTH_FORMAT,
        .width = viewport.x,
        .height = viewport.y,
    });
  }
  RgTextureId depth_buffer = pass_rcs.depth_buffer;
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

  RgTextureId ssao_llm;
  RgTextureId ssao_depth = pass_rcs.ssao_depth;
  if (m_data.settings.ssao) {
    glm::uvec2 ssao_hi_z_size = {std::bit_floor(viewport.x),
                                 std::bit_floor(viewport.y)};
    // Don't need the full mip chain since after a certain stage they become
    // small enough to completely fit in cache but are much less detailed.
    i32 num_ssao_hi_z_mips =
        get_mip_chain_length(std::min(ssao_hi_z_size.x, ssao_hi_z_size.y));
    num_ssao_hi_z_mips =
        std::max<i32>(num_ssao_hi_z_mips - get_mip_chain_length(32) + 1, 1);

    if (!pass_rcs.ssao_hi_z) {
      pass_rcs.ssao_hi_z = rgp.create_texture({
          .name = "ssao-hi-z",
          .format = TinyImageFormat_R16_SFLOAT,
          .width = ssao_hi_z_size.x,
          .height = ssao_hi_z_size.y,
          .num_mips = (u32)num_ssao_hi_z_mips,
      });
    }
    RgTextureId ssao_hi_z = pass_rcs.ssao_hi_z;

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
          m_sid->m_pipelines.ssao_hi_z, args, ssao_hi_z_size,
          {glsl::SPD_THREAD_ELEMS_X, glsl::SPD_THREAD_ELEMS_Y});
    }

    glm::uvec2 ssao_size =
        m_data.settings.ssao_full_res ? viewport : viewport / 2u;

    if (!pass_rcs.ssao) {
      pass_rcs.ssao = rgp.create_texture({
          .name = "ssao",
          .format = TinyImageFormat_R16_UNORM,
          .width = ssao_size.x,
          .height = ssao_size.y,
      });
    }
    if (!m_data.settings.ssao_full_res and !pass_rcs.ssao_depth) {
      pass_rcs.ssao_depth = rgp.create_texture({
          .name = "ssao-depth",
          .format = TinyImageFormat_R16_SFLOAT,
          .width = ssao_size.x,
          .height = ssao_size.y,
      });
    }

    RgTextureId ssao = pass_rcs.ssao;
    ssao_depth = pass_rcs.ssao_depth;
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
      pass.dispatch_grid_2d(m_sid->m_pipelines.ssao, args, ssao_size);
    }

    if (!pass_rcs.ssao_llm) {
      pass_rcs.ssao_llm = rgp.create_texture({
          .name = "ssao-llm",
          .format = TinyImageFormat_R16G16_SFLOAT,
          .width = ssao_size.x,
          .height = ssao_size.y,
      });
    }
    ssao_llm = pass_rcs.ssao_llm;
    {
      auto pass = rgb.create_pass({.name = "ssao-filter"});

      RgSsaoFilterArgs args = {
          .depth =
              !ssao_depth ? pass.read_texture(depth_buffer) : RgTextureToken(),
          .ssao = pass.read_texture(ssao),
          .ssao_depth = pass.try_read_texture(ssao_depth),
          .ssao_llm = pass.write_texture("ssao-llm", &ssao_llm),
          .znear = camera.near,
      };
      pass.dispatch(m_sid->m_pipelines.ssao_filter, args,
                    ceil_div(ssao_size.x, glsl::SSAO_FILTER_GROUP_SIZE.x *
                                              glsl::SSAO_FILTER_UNROLL.x),
                    ceil_div(ssao_size.y, glsl::SSAO_FILTER_GROUP_SIZE.y *
                                              glsl::SSAO_FILTER_UNROLL.y));
    }
  }

  if (!pass_rcs.hdr) {
    pass_rcs.hdr = rgp.create_texture({
        .name = "hdr",
        .format = HDR_FORMAT,
        .width = viewport.x,
        .height = viewport.y,
    });
  }
  RgTextureId hdr = pass_rcs.hdr;

  setup_opaque_pass(cfg, OpaquePassConfig{
                             .gpu_scene = &m_gpu_scene,
                             .rg_gpu_scene = &rg_gpu_scene,
                             .occlusion_culling_mode = occlusion_culling_mode,
                             .hdr = &hdr,
                             .depth_buffer = &depth_buffer,
                             .hi_z = hi_z,
                             .ssao = ssao_llm,
                             .exposure = exposure,
                         });

  setup_skybox_pass(cfg, SkyboxPassConfig{
                             .exposure = exposure,
                             .hdr = &hdr,
                             .depth_buffer = depth_buffer,
                         });

  if (!cfg.rcs->acquire_semaphore) {
    cfg.rcs->acquire_semaphore = rgp.create_semaphore("acquire-semaphore");
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

  m_swap_chain->set_usage(swap_chain_usage);

  setup_present_pass(cfg, PresentPassConfig{
                              .src = sdr,
                              .acquire_semaphore = m_frcs->acquire_semaphore,
                              .swap_chain = m_swap_chain,
                          });

  return rgb.build({
      .gfx_allocator = &m_sid->m_gfx_allocator,
      .async_allocator = &m_sid->m_async_allocator,
      .shared_allocator = &m_sid->m_shared_allocators[0],
      .upload_allocator = &m_frcs->upload_allocator,
  });
}

} // namespace ren

namespace ren::hot_reload {

void unload(Scene *scene) {
  scene->m_renderer->wait_idle();
  scene->m_sid = nullptr;
  unload(scene->m_renderer);
}

auto load(Scene *scene) -> expected<void> {
  ren_try_to(load(scene->m_renderer));
  ren_try(scene->m_sid, init_scene_internal_data(
                            scene->m_renderer, scene->m_descriptor_allocator));
  ren_try_to(scene->next_frame());
  return {};
}

} // namespace ren::hot_reload
