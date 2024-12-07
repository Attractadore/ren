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
    : m_arena(renderer), m_fif_arena(renderer),
      m_device_allocator(renderer, m_arena, 256 * 1024 * 1024) {
  m_renderer = &renderer;
  m_swapchain = &swapchain;

  Handle<DescriptorSetLayout> texture_descriptor_set_layout =
      create_persistent_descriptor_set_layout(m_arena);

  VkDescriptorSet texture_descriptor_set;
  std::tie(std::ignore, texture_descriptor_set) =
      allocate_descriptor_pool_and_set(*m_renderer, m_arena,
                                       texture_descriptor_set_layout);

  m_descriptor_allocator = std::make_unique<DescriptorAllocator>(
      texture_descriptor_set, texture_descriptor_set_layout);

  m_samplers = {
      .hi_z_gen = m_arena.create_sampler({
          .name = "Hi-Z generation sampler",
          .mag_filter = VK_FILTER_LINEAR,
          .min_filter = VK_FILTER_LINEAR,
          .mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
          .address_mode_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
          .address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
          .reduction_mode = SamplerReductionMode::Min,
      }),
      .hi_z = m_arena.create_sampler({
          .name = "Hi-Z generation sampler",
          .mag_filter = VK_FILTER_NEAREST,
          .min_filter = VK_FILTER_NEAREST,
          .mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
          .address_mode_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
          .address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      }),
  };

  m_pipelines = load_pipelines(m_arena, texture_descriptor_set_layout);

  m_rgp = std::make_unique<RgPersistent>(*m_renderer);

  m_gpu_scene = init_gpu_scene(m_arena);

  m_data.settings.amd_anti_lag =
      m_renderer->is_feature_supported(RendererFeature::AmdAntiLag);

  next_frame();
}

void Scene::allocate_per_frame_resources() {
  m_fif_arena.clear();
  m_per_frame_resources.clear();
  m_new_num_frames_in_flight = m_num_frames_in_flight;
  for (auto i : range(m_num_frames_in_flight)) {
    m_per_frame_resources.emplace_back(ScenePerFrameResources{
        .acquire_semaphore = m_fif_arena.create_semaphore({
            .name = fmt::format("Acquire semaphore {}", i),
        }),
        .present_semaphore = m_fif_arena.create_semaphore({
            .name = fmt::format("Present semaphore {}", i),
        }),
        .upload_allocator =
            UploadBumpAllocator(*m_renderer, m_fif_arena, 64 * 1024 * 1024),
        .cmd_allocator = CommandAllocator(*m_renderer),
        .descriptor_allocator =
            DescriptorAllocatorScope(*m_descriptor_allocator),
    });
  }
  m_graphics_time = m_num_frames_in_flight;
  m_graphics_semaphore = m_fif_arena.create_semaphore({
      .name = "Graphics queue timeline semaphore",
      .initial_value = m_graphics_time - 1,
  });
  m_swapchain->set_frames_in_flight(m_num_frames_in_flight);
}

void ScenePerFrameResources::reset() {
  upload_allocator.reset();
  cmd_allocator.reset();
  descriptor_allocator.reset();
}

void Scene::next_frame() {
  ren_prof_zone("Scene::next_frame");

  m_frame_index++;

  m_num_frames_in_flight = m_new_num_frames_in_flight;
  [[unlikely]] if (m_per_frame_resources.size() != m_num_frames_in_flight) {
    allocate_per_frame_resources();
  } else {
    m_renderer->graphicsQueueSubmit(
        {}, {},
        {{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = m_renderer->get_semaphore(m_graphics_semaphore).handle,
            .value = m_graphics_time,
        }});
    m_graphics_time++;
    {
      ren_prof_zone("Scene::wait_for_previous_frame");
      ren_prof_zone_text(
          fmt::format("{}", i64(m_frame_index - m_num_frames_in_flight)));
      u64 wait_time = m_graphics_time - m_num_frames_in_flight;
      m_renderer->wait_for_semaphore(
          m_renderer->get_semaphore(m_graphics_semaphore), wait_time);
    }
  }
  m_frcs = &m_per_frame_resources[m_graphics_time % m_num_frames_in_flight];
  m_frcs->reset();

  VkCommandBuffer cmd = m_frcs->cmd_allocator.allocate();
  {
    CommandRecorder rec(*m_renderer, cmd);
    auto _ = rec.debug_region("begin-frame");
    m_device_allocator.reset(rec);
  }
  m_renderer->graphicsQueueSubmit({{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
      .commandBuffer = cmd,
  }});
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
                                       Handle<Buffer> &buffer, DebugName name) {
    if (not data.empty()) {
      BufferSlice<T> slice = m_arena.create_buffer<T>({
          .name = std::move(name),
          .heap = BufferHeap::Static,
          .usage = VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR |
                   VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR,
          .count = data.size(),
      });
      buffer = slice.buffer;
      m_resource_uploader.stage_buffer(*m_renderer, m_frcs->upload_allocator,
                                       Span(data), slice);
    }
  };

  u32 index = m_data.meshes.size();

  upload_buffer(positions, mesh.positions,
                fmt::format("Mesh {} positions", index));
  upload_buffer(normals, mesh.normals, fmt::format("Mesh {} normals", index));
  upload_buffer(tangents, mesh.tangents,
                fmt::format("Mesh {} tangents", index));
  upload_buffer(uvs, mesh.uvs, fmt::format("Mesh {} uvs", index));
  upload_buffer(colors, mesh.colors, fmt::format("Mesh {} colors", index));

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

  upload_buffer(meshlets, mesh.meshlets,
                fmt::format("Mesh {} meshlets", index));
  upload_buffer(meshlet_indices, mesh.meshlet_indices,
                fmt::format("Mesh {} meshlet indices", index));

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
    -> Handle<Sampler> {
  Handle<Sampler> &handle = m_sampler_cache[create_info];
  if (!handle) {
    handle = m_arena.create_sampler(std::move(create_info));
  }
  return handle;
}

auto Scene::get_or_create_texture(Handle<Image> image,
                                  const SamplerDesc &sampler_desc)
    -> glsl::SampledTexture2D {
  TextureView view = m_renderer->get_texture_view(m_images[image]);
  Handle<Sampler> sampler = get_or_create_sampler({
      .mag_filter = getVkFilter(sampler_desc.mag_filter),
      .min_filter = getVkFilter(sampler_desc.min_filter),
      .mipmap_mode = getVkSamplerMipmapMode(sampler_desc.mipmap_filter),
      .address_mode_u = getVkSamplerAddressMode(sampler_desc.wrap_u),
      .address_mode_v = getVkSamplerAddressMode(sampler_desc.wrap_v),
      .anisotropy = 16.0f,
  });
  return glsl::SampledTexture2D(
      m_descriptor_allocator->allocate_sampled_texture(*m_renderer, view,
                                                       sampler));
}

auto Scene::create_image(const ImageCreateInfo &desc) -> expected<ImageId> {
  auto texture = m_arena.create_texture({
      .type = VK_IMAGE_TYPE_2D,
      .format = desc.format,
      .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
               VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
      .width = desc.width,
      .height = desc.height,
      .num_mip_levels = get_mip_level_count(desc.width, desc.height),
  });

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
      [&](const auto &texture) -> glsl::SampledTexture2D {
    if (texture.image) {
      return get_or_create_texture(std::bit_cast<Handle<Image>>(texture.image),
                                   texture.sampler);
    }
    return {};
  };

  Handle<Material> handle = m_data.materials.insert({
      .base_color = desc.base_color_factor,
      .base_color_texture = get_sampled_texture_id(desc.base_color_texture),
      .metallic = desc.metallic_factor,
      .roughness = desc.roughness_factor,
      .metallic_roughness_texture =
          get_sampled_texture_id(desc.metallic_roughness_texture),
      .normal_texture = get_sampled_texture_id(desc.normal_texture),
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
    m_renderer->amd_anti_lag(m_frame_index, VK_ANTI_LAG_STAGE_INPUT_AMD);
  }
  return {};
}

bool Scene::is_amd_anti_lag_available() {
  return m_renderer->is_feature_supported(RendererFeature::AmdAntiLag) and
         m_num_frames_in_flight <= 2;
}

bool Scene::is_amd_anti_lag_enabled() {
  return is_amd_anti_lag_available() and m_data.settings.amd_anti_lag;
}

auto Scene::draw() -> expected<void> {
  ren_prof_zone("Scene::draw");

  m_resource_uploader.upload(*m_renderer, m_frcs->cmd_allocator);

  RenderGraph render_graph = build_rg();

  render_graph.execute(m_frcs->cmd_allocator);

  if (is_amd_anti_lag_enabled()) {
    m_renderer->amd_anti_lag(m_frame_index, VK_ANTI_LAG_STAGE_PRESENT_AMD);
  }

  m_swapchain->present(m_frcs->present_semaphore);

  prof::mark_frame();

  next_frame();

  return {};
}

#if REN_IMGUI
void Scene::draw_imgui() {
  ren_prof_zone("Scene::draw_imgui");

  ren_ImGuiScope(m_imgui_context);
  if (ImGui::GetCurrentContext()) {
    if (ImGui::Begin("Scene renderer settings")) {
      SceneGraphicsSettings &settings = m_data.settings;

      ImGui::SeparatorText("Latency");
      {
        int fif = m_num_frames_in_flight;
        ImGui::SliderInt("Frames in flight", &fif, 1, 4);
        m_new_num_frames_in_flight = fif;
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
      get_or_create_texture(std::bit_cast<Handle<Image>>(image), desc);
  // NOTE: texture from old context is leaked. Don't really care since context
  // will probably be set only once
  io.Fonts->SetTexID((ImTextureID)(uintptr_t)texture);
}
#endif

auto Scene::build_rg() -> RenderGraph {
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

  set_if_changed(m_pass_cfg.exposure, m_data.exposure.mode);

  set_if_changed(m_pass_cfg.viewport, m_swapchain->get_size());

  set_if_changed(m_pass_cfg.backbuffer_usage, m_swapchain->get_usage());

  if (dirty) {
    m_rgp->reset();
    m_pass_rcs = {};
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
  u32 exposure_temporal_layer = 0;
  setup_exposure_pass(cfg, ExposurePassConfig{
                               .exposure = &exposure,
                               .temporal_layer = &exposure_temporal_layer,
                           });

  RgTextureId depth_buffer;
  RgTextureId hdr;
  setup_opaque_passes(cfg,
                      OpaquePassesConfig{
                          .gpu_scene = &m_gpu_scene,
                          .rg_gpu_scene = &rg_gpu_scene,
                          .exposure = exposure,
                          .exposure_temporal_layer = exposure_temporal_layer,
                          .depth_buffer = &depth_buffer,
                          .hdr = &hdr,
                      });

  RgTextureId sdr;
  setup_post_processing_passes(cfg, PostProcessingPassesConfig{
                                        .hdr = hdr,
                                        .exposure = exposure,
                                        .sdr = &sdr,
                                    });
#if REN_IMGUI
  if (m_imgui_context) {
    setup_imgui_pass(cfg, ImGuiPassConfig{
                              .ctx = m_imgui_context,
                              .sdr = &sdr,
                          });
  }
#endif

  setup_present_pass(cfg, PresentPassConfig{
                              .src = sdr,
                              .acquire_semaphore = m_frcs->acquire_semaphore,
                              .present_semaphore = m_frcs->present_semaphore,
                              .swapchain = m_swapchain,
                          });

  RenderGraph rg = rgb.build(m_device_allocator, m_frcs->upload_allocator);

  rg_export_gpu_scene(rgb, rg_gpu_scene, &m_gpu_scene);

  return rg;
}

} // namespace ren
