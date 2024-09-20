#include "Scene.hpp"
#include "CommandRecorder.hpp"
#include "Formats.hpp"
#include "ImGuiConfig.hpp"
#include "MeshProcessing.hpp"
#include "Passes/Exposure.hpp"
#include "Passes/ImGui.hpp"
#include "Passes/Opaque.hpp"
#include "Passes/PostProcessing.hpp"
#include "Passes/Present.hpp"
#include "Support/Span.hpp"
#include "Support/Views.hpp"
#include "Swapchain.hpp"
#include "glsl/InstanceCullingAndLODPass.h"
#include "glsl/MeshletCullingPass.h"

#include <fmt/format.h>

namespace ren {

Scene::Scene(Renderer &renderer, Swapchain &swapchain)
    : m_arena(renderer), m_fif_arena(renderer),
      m_device_allocator(renderer, m_arena, 64 * 1024 * 1024) {
  m_renderer = &renderer;
  m_swapchain = &swapchain;

  m_persistent_descriptor_set_layout =
      create_persistent_descriptor_set_layout(m_arena);
  std::tie(m_persistent_descriptor_pool, m_persistent_descriptor_set) =
      allocate_descriptor_pool_and_set(*m_renderer, m_arena,
                                       m_persistent_descriptor_set_layout);

  m_texture_allocator = std::make_unique<TextureIdAllocator>(
      m_persistent_descriptor_set, m_persistent_descriptor_set_layout);

  m_pipelines = load_pipelines(m_arena, m_persistent_descriptor_set_layout);

  m_rgp = std::make_unique<RgPersistent>(*m_renderer, *m_texture_allocator);

  allocate_per_frame_resources();
}

auto Scene::get_exposure_mode() const -> ExposureMode {
  return m_exposure_mode;
}

auto Scene::get_exposure_compensation() const -> float {
  return m_exposure_compensation;
}

auto Scene::get_camera() const -> const Camera & { return m_cameras[m_camera]; }

auto Scene::get_camera_proj_view() const -> glm::mat4 {
  const Camera &camera = get_camera();
  return get_projection_matrix(camera, get_viewport()) *
         get_view_matrix(camera);
}

auto Scene::get_viewport() const -> glm::uvec2 {
  return m_swapchain->get_size();
}

auto Scene::get_meshes() const -> const GenArray<Mesh> & { return m_meshes; }

auto Scene::get_index_pools() const -> Span<const IndexPool> {
  return m_index_pools;
}

auto Scene::get_materials() const -> const GenArray<Material> & {
  return m_materials;
}

auto Scene::get_mesh_instances() const -> const GenArray<MeshInstance> & {
  return m_mesh_instances;
}

auto Scene::get_mesh_instance_transforms() const
    -> const GenMap<glm::mat4x3, Handle<MeshInstance>> & {
  return m_mesh_instance_transforms;
}

auto Scene::get_directional_lights() const -> const GenArray<glsl::DirLight> & {
  return m_dir_lights;
}

bool Scene::is_early_z_enabled() const { return m_early_z; }

auto Scene::get_draw_size() const -> u32 { return m_draw_size; }

auto Scene::get_num_draw_meshlets() const -> u32 { return m_num_draw_meshlets; }

bool Scene::is_frustum_culling_enabled() const {
  return m_instance_frustum_culling;
}

bool Scene::is_lod_selection_enabled() const { return m_lod_selection; }

auto Scene::get_lod_triangle_pixel_count() const -> float {
  return m_lod_triangle_pixels;
}

auto Scene::get_lod_bias() const -> i32 { return m_lod_bias; }

auto Scene::get_instance_culling_and_lod_feature_mask() const -> u32 {
  u32 feature_mask = 0;
  if (is_frustum_culling_enabled()) {
    feature_mask |= glsl::INSTANCE_CULLING_AND_LOD_FRUSTUM_BIT;
  }
  if (is_lod_selection_enabled()) {
    feature_mask |= glsl::INSTANCE_CULLING_AND_LOD_LOD_SELECTION_BIT;
  }
  return feature_mask;
}

bool Scene::is_meshlet_cone_culling_enabled() const {
  return m_meshlet_cone_culling;
}

bool Scene::is_meshlet_frustum_culling_enabled() const {
  return m_meshlet_frustum_culling;
}

auto Scene::get_meshlet_culling_feature_mask() const -> u32 {
  u32 mask = 0;
  if (is_meshlet_cone_culling_enabled()) {
    mask |= glsl::MESHLET_CULLING_CONE_BIT;
  }
  if (is_meshlet_frustum_culling_enabled()) {
    mask |= glsl::MESHLET_CULLING_FRUSTUM_BIT;
  }
  return mask;
}

void Scene::allocate_per_frame_resources() {
  m_fif_arena.clear();
  m_per_frame_resources.clear();
  m_new_num_frames_in_flight = m_num_frames_in_flight;
  for (auto i : range(m_num_frames_in_flight)) {
    m_per_frame_resources.emplace_back(FrameResources{
        .acquire_semaphore = m_fif_arena.create_semaphore({
            .name = fmt::format("Acquire semaphore {}", i),
        }),
        .present_semaphore = m_fif_arena.create_semaphore({
            .name = fmt::format("Present semaphore {}", i),
        }),
        .upload_allocator =
            UploadBumpAllocator(*m_renderer, m_fif_arena, 64 * 1024 * 1024),
        .cmd_allocator = CommandAllocator(*m_renderer),
    });
  }
  m_graphics_time = m_num_frames_in_flight;
  m_graphics_semaphore = m_fif_arena.create_semaphore({
      .name = "Graphics queue timeline semaphore",
      .initial_value = m_graphics_time - 1,
  });
}

void FrameResources::reset() {
  upload_allocator.reset();
  cmd_allocator.reset();
}

void Scene::next_frame() {
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
    m_renderer->wait_for_semaphore(
        m_renderer->get_semaphore(m_graphics_semaphore),
        m_graphics_time - m_num_frames_in_flight);
    get_frame_resources().reset();
  }

  VkCommandBuffer cmd = get_frame_resources().cmd_allocator.allocate();
  {
    CommandRecorder rec(*m_renderer, cmd);
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
      m_resource_uploader.stage_buffer(*m_renderer,
                                       get_frame_resources().upload_allocator,
                                       Span(data), slice);
    }
  };

  u32 index = m_meshes.size();

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

  if (m_index_pools.empty() or
      m_index_pools.back().num_free_indices < num_triangles * 3) {
    m_index_pools.emplace_back(create_index_pool(m_arena));
  }

  mesh.index_pool = m_index_pools.size() - 1;
  IndexPool &index_pool = m_index_pools.back();

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
      *m_renderer, get_frame_resources().upload_allocator,
      Span(meshlet_triangles),
      m_renderer->get_buffer_slice<u8>(index_pool.indices)
          .slice(base_triangle, num_triangles * 3));

  Handle<Mesh> h = m_meshes.insert(mesh);

  return std::bit_cast<MeshId>(h);
}

auto Scene::get_or_create_sampler(const SamplerCreateInfo &&create_info)
    -> Handle<Sampler> {
  Handle<Sampler> &handle = m_samplers[create_info];
  if (!handle) {
    handle = m_arena.create_sampler(std::move(create_info));
  }
  return handle;
}

auto Scene::get_or_create_texture(
    Handle<Image> image, const SamplerDesc &sampler_desc) -> SampledTextureId {
  TextureView view = m_renderer->get_texture_view(m_images[image]);
  Handle<Sampler> sampler = get_or_create_sampler({
      .mag_filter = getVkFilter(sampler_desc.mag_filter),
      .min_filter = getVkFilter(sampler_desc.min_filter),
      .mipmap_mode = getVkSamplerMipmapMode(sampler_desc.mipmap_filter),
      .address_mode_u = getVkSamplerAddressMode(sampler_desc.wrap_u),
      .address_mode_v = getVkSamplerAddressMode(sampler_desc.wrap_v),
      .anisotropy = 16.0f,
  });
  return m_texture_allocator->allocate_sampled_texture(*m_renderer, view,
                                                       sampler);
}

auto Scene::create_image(const ImageCreateInfo &desc) -> expected<ImageId> {
  auto format = getVkFormat(desc.format);
  auto texture = m_arena.create_texture({
      .type = VK_IMAGE_TYPE_2D,
      .format = format,
      .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
               VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
      .width = desc.width,
      .height = desc.height,
      .num_mip_levels = get_mip_level_count(desc.width, desc.height),
  });
  usize size = desc.width * desc.height * get_format_size(format);
  m_resource_uploader.stage_texture(
      *m_renderer, get_frame_resources().upload_allocator,
      Span((const std::byte *)desc.data, size), texture);
  Handle<Image> h = m_images.insert(texture);
  return std::bit_cast<ImageId>(h);
}

auto Scene::create_material(const MaterialCreateInfo &desc)
    -> expected<MaterialId> {
  auto get_sampled_texture_id = [&](const auto &texture) -> u32 {
    if (texture.image) {
      return get_or_create_texture(std::bit_cast<Handle<Image>>(texture.image),
                                   texture.sampler);
    }
    return 0;
  };

  Handle<glsl::Material> h = m_materials.insert({
      .base_color = desc.base_color_factor,
      .base_color_texture = get_sampled_texture_id(desc.base_color_texture),
      .metallic = desc.metallic_factor,
      .roughness = desc.roughness_factor,
      .metallic_roughness_texture =
          get_sampled_texture_id(desc.metallic_roughness_texture),
      .normal_texture = get_sampled_texture_id(desc.normal_texture),
      .normal_scale = desc.normal_texture.scale,
  });

  return std::bit_cast<MaterialId>(h);
  ;
}

auto Scene::create_camera() -> expected<CameraId> {
  return std::bit_cast<CameraId>(m_cameras.emplace());
}

void Scene::destroy_camera(CameraId camera) {
  m_cameras.erase(std::bit_cast<Handle<Camera>>(camera));
}

auto Scene::get_camera(CameraId camera) -> Camera & {
  return m_cameras[std::bit_cast<Handle<Camera>>(camera)];
}

void Scene::set_camera(CameraId camera) {
  m_camera = std::bit_cast<Handle<Camera>>(camera);
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
  m_exposure_mode = desc.mode;
  m_exposure_compensation = desc.ec;
};

auto Scene::create_mesh_instances(
    std::span<const MeshInstanceCreateInfo> create_info,
    std::span<MeshInstanceId> out) -> expected<void> {
  ren_assert(out.size() >= create_info.size());
  for (usize i : range(create_info.size())) {
    ren_assert(create_info[i].mesh);
    ren_assert(create_info[i].material);
    const Mesh &mesh =
        m_meshes[std::bit_cast<Handle<Mesh>>(create_info[i].mesh)];
    Handle<MeshInstance> mesh_instance = m_mesh_instances.insert({
        .mesh = std::bit_cast<Handle<Mesh>>(create_info[i].mesh),
        .material = std::bit_cast<Handle<Material>>(create_info[i].material),
    });
    m_mesh_instance_transforms.insert(
        mesh_instance, glsl::make_decode_position_matrix(mesh.pos_enc_bb));
    out[i] = std::bit_cast<MeshInstanceId>(mesh_instance);
  }
  return {};
}

void Scene::destroy_mesh_instances(
    std::span<const MeshInstanceId> mesh_instances) {
  for (MeshInstanceId mesh_instance : mesh_instances) {
    m_mesh_instances.erase(std::bit_cast<Handle<MeshInstance>>(mesh_instance));
  }
}

void Scene::set_mesh_instance_transforms(
    std::span<const MeshInstanceId> mesh_instances,
    std::span<const glm::mat4x3> matrices) {
  ren_assert(mesh_instances.size() == matrices.size());
  for (usize i : range(mesh_instances.size())) {
    auto h = std::bit_cast<Handle<MeshInstance>>(mesh_instances[i]);
    MeshInstance &mesh_instance = m_mesh_instances[h];
    const Mesh &mesh =
        m_meshes[std::bit_cast<Handle<Mesh>>(mesh_instance.mesh)];
    m_mesh_instance_transforms[h] =
        matrices[i] * glsl::make_decode_position_matrix(mesh.pos_enc_bb);
  }
}

auto Scene::create_directional_light(const DirectionalLightDesc &desc)
    -> expected<DirectionalLightId> {
  Handle<glsl::DirLight> light = m_dir_lights.emplace();
  auto id = std::bit_cast<DirectionalLightId>(light);
  set_directional_light(id, desc);
  return id;
};

void Scene::destroy_directional_light(DirectionalLightId light) {
  m_dir_lights.erase(std::bit_cast<Handle<glsl::DirLight>>(light));
}

void Scene::set_directional_light(DirectionalLightId light,
                                  const DirectionalLightDesc &desc) {
  m_dir_lights[std::bit_cast<Handle<glsl::DirLight>>(light)] = {
      .color = desc.color,
      .illuminance = desc.illuminance,
      .origin = glm::normalize(desc.origin),
  };
};

auto Scene::draw() -> expected<void> {
  FrameResources &fr = get_frame_resources();

  m_resource_uploader.upload(*m_renderer, fr.cmd_allocator);

  RenderGraph render_graph = build_rg();

  render_graph.execute(fr.cmd_allocator);

  m_swapchain->present(fr.present_semaphore);

  next_frame();

  return {};
}

#if REN_IMGUI
void Scene::draw_imgui() {
  ren_ImGuiScope(m_imgui_context);
  if (ImGui::GetCurrentContext()) {
    if (ImGui::Begin("Scene renderer settings")) {
      {
        int fif = m_num_frames_in_flight;
        ImGui::SliderInt("Frames in flight", &fif, 1, 4);
        m_new_num_frames_in_flight = fif;
      }
      {
        int draw_size = m_draw_size;
        ImGui::SliderInt("Maximum indirect draw instance count", &draw_size, 1,
                         128 * 1024);
        m_draw_size = draw_size;
      }
      {
        int num_draw_meshlets = m_num_draw_meshlets;
        ImGui::SliderInt("Maximum indirect draw instance count",
                         &num_draw_meshlets, 1, 1024 * 1024);
        m_num_draw_meshlets = num_draw_meshlets;
      }

      ImGui::SeparatorText("Instance culling");
      {
        bool frustum = m_instance_frustum_culling;
        ImGui::Checkbox("Frustum culling## Instance", &frustum);
        m_instance_frustum_culling = frustum;
      }

      ImGui::SeparatorText("Level of detail");
      {
        ImGui::SliderInt("LOD bias", &m_lod_bias, -(glsl::MAX_NUM_LODS - 1),
                         glsl::MAX_NUM_LODS - 1, "%d");

        bool selection = m_lod_selection;
        ImGui::Checkbox("LOD selection", &selection);
        m_lod_selection = selection;

        ImGui::BeginDisabled(!m_lod_selection);
        ImGui::SliderFloat("LOD pixels per triangle", &m_lod_triangle_pixels,
                           1.0f, 64.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
        ImGui::EndDisabled();
      }

      ImGui::SeparatorText("Meshlet culling");
      {
        bool cone = m_meshlet_cone_culling;
        ImGui::Checkbox("Cone culling", &cone);
        m_meshlet_cone_culling = cone;

        bool frustum = m_meshlet_frustum_culling;
        ImGui::Checkbox("Frustum culling## Meshlet", &frustum);
        m_meshlet_frustum_culling = frustum;
      }

      ImGui::SeparatorText("Opaque pass");
      {
        bool early_z = m_early_z;
        ImGui::Checkbox("Early Z", &early_z);
        m_early_z = early_z;
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
  ren::ImageId image = create_image({
                                        .width = u32(width),
                                        .height = u32(height),
                                        .format = Format::RGBA8_UNORM,
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
  SampledTextureId texture =
      get_or_create_texture(std::bit_cast<Handle<Image>>(image), desc);
  // NOTE: texture from old context is leaked. Don't really care since context
  // will probably be set only once
  io.Fonts->SetTexID((ImTextureID)(uintptr_t)texture);
}
#endif

auto Scene::build_rg() -> RenderGraph {
  bool dirty = false;
  auto set_if_changed =
      [&]<typename T>(T &config_value,
                      const std::convertible_to<T> auto &new_value) {
        if (config_value != new_value) {
          config_value = new_value;
          dirty = true;
        }
      };

  set_if_changed(m_pass_cfg.viewport, get_viewport());

  set_if_changed(m_pass_cfg.exposure, m_exposure_mode);

  set_if_changed(m_pass_cfg.backbuffer_usage, m_swapchain->get_usage());

  if (dirty) {
    m_rgp->reset();
    m_pass_rcs = {};
  }

  RgBuilder rgb(*m_rgp, *m_renderer);

  PassCommonConfig cfg = {
      .rgp = m_rgp.get(),
      .rgb = &rgb,
      .allocator = &get_frame_resources().upload_allocator,
      .pipelines = &m_pipelines,
      .scene = this,
      .rcs = &m_pass_rcs,
  };

  RgTextureId exposure;
  u32 exposure_temporal_layer = 0;
  setup_exposure_pass(cfg, ExposurePassConfig{
                               .exposure = &exposure,
                               .temporal_layer = &exposure_temporal_layer,
                           });

  RgTextureId hdr;
  setup_opaque_passes(cfg,
                      OpaquePassesConfig{
                          .exposure = exposure,
                          .exposure_temporal_layer = exposure_temporal_layer,
                          .hdr = &hdr,
                      });

  RgTextureId sdr;
  setup_post_processing_passes(cfg, PostProcessingPassesConfig{
                                        .hdr = hdr,
                                        .exposure = exposure,
                                        .sdr = &sdr,
                                    });
#if REN_IMGUI
  if (get_imgui_context()) {
    setup_imgui_pass(cfg, ImGuiPassConfig{.sdr = &sdr});
  }
#endif

  FrameResources &fr = get_frame_resources();

  setup_present_pass(cfg, PresentPassConfig{
                              .src = sdr,
                              .acquire_semaphore = fr.acquire_semaphore,
                              .present_semaphore = fr.present_semaphore,
                              .swapchain = m_swapchain,
                          });

  return rgb.build(m_device_allocator, fr.upload_allocator);
}

} // namespace ren
