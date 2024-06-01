#include "Scene.hpp"
#include "Formats.hpp"
#include "ImGuiConfig.hpp"
#include "MeshProcessing.hpp"
#include "Passes.hpp"
#include "Support/Errors.hpp"
#include "Support/Views.hpp"
#include "Swapchain.hpp"

#include <range/v3/algorithm.hpp>
#include <range/v3/numeric.hpp>
#include <range/v3/range.hpp>

namespace ren {

Scene::Scene(Renderer &renderer, Swapchain &swapchain)
    : m_cmd_allocator(renderer), m_arena(renderer), m_frame_arena(renderer) {
  m_renderer = &renderer;
  m_swapchain = &swapchain;

  m_persistent_descriptor_set_layout =
      create_persistent_descriptor_set_layout(m_arena);
  std::tie(m_persistent_descriptor_pool, m_persistent_descriptor_set) =
      allocate_descriptor_pool_and_set(*m_renderer, m_arena,
                                       m_persistent_descriptor_set_layout);

  m_texture_allocator = std::make_unique<TextureIdAllocator>(
      m_persistent_descriptor_set, m_persistent_descriptor_set_layout);

  m_render_graph = std::make_unique<RenderGraph>(*m_renderer, swapchain,
                                                 *m_texture_allocator);

  m_pipelines = load_pipelines(m_arena, m_persistent_descriptor_set_layout);

  // TODO: delete when Clang implements constexpr std::bit_cast for structs
  // with bitfields
#define error "C handles can't be directly converted to SlotMap keys"
#if !BOOST_COMP_CLANG
  static_assert(std::bit_cast<u32>(SlotMapKey()) == 0, error);
#else
  ren_assert_msg(std::bit_cast<u32>(SlotMapKey()) == 0, error);
#endif
#undef error
}

void Scene::next_frame() {
  m_renderer->next_frame();
  m_cmd_allocator.next_frame();
  m_texture_allocator->next_frame();
}

auto Scene::create_mesh(const MeshCreateInfo &desc) -> expected<MeshId> {
  Vector<glsl::Position> positions;
  Vector<glsl::Normal> normals;
  Vector<glsl::Tangent> tangents;
  Vector<glsl::UV> uvs;
  Vector<glsl::Color> colors;
  Vector<u32> indices = desc.indices;

  Mesh mesh = mesh_process(MeshProcessingOptions{
      .positions = desc.positions,
      .normals = desc.normals,
      .tangents = desc.tangents,
      .uvs = desc.uvs,
      .colors = desc.colors,
      .enc_positions = &positions,
      .enc_normals = &normals,
      .enc_tangents = &tangents,
      .enc_uvs = &uvs,
      .enc_colors = &colors,
      .indices = &indices,
  });

  // Upload vertices

  auto create_stream = [&]<typename T>(const Vector<T> &stream,
                                       Handle<Buffer> &buffer, DebugName name) {
    if (not stream.empty()) {
      BufferView view = m_arena.create_buffer({
          .name = std::move(name),
          .heap = BufferHeap::Static,
          .usage = VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR |
                   VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR,
          .size = Span(stream).size_bytes(),
      });
      buffer = view.buffer;
      m_resource_uploader.stage_buffer(*m_renderer, m_frame_arena, Span(stream),
                                       view);
    }
  };

  u32 index = m_meshes.size();

  create_stream(positions, mesh.positions,
                fmt::format("Mesh {} positions", index));
  create_stream(normals, mesh.normals, fmt::format("Mesh {} normals", index));
  create_stream(tangents, mesh.tangents,
                fmt::format("Mesh {} tangents", index));
  create_stream(uvs, mesh.uvs, fmt::format("Mesh {} uvs", index));
  create_stream(colors, mesh.colors, fmt::format("Mesh {} colors", index));

  // Find or allocate index pool

  ren_assert_msg(mesh.num_indices <= glsl::INDEX_POOL_SIZE,
                 "Index pool overflow");

  if (m_index_pools.empty() or
      m_index_pools.back().num_free_indices < mesh.num_indices) {
    m_index_pools.emplace_back(create_index_pool(m_arena));
  }

  mesh.index_pool = m_index_pools.size() - 1;
  IndexPool &index_pool = m_index_pools.back();

  mesh.base_index = glsl::INDEX_POOL_SIZE - index_pool.num_free_indices;
  for (glsl::MeshLOD &lod : mesh.lods) {
    lod.base_index += mesh.base_index;
  }

  index_pool.num_free_indices -= mesh.num_indices;

  // Upload indices

  auto indices_dst = m_renderer->get_buffer_view(index_pool.indices)
                         .slice<u32>(mesh.base_index, mesh.num_indices);
  m_resource_uploader.stage_buffer(*m_renderer, m_frame_arena, Span(indices),
                                   indices_dst);

  auto key = std::bit_cast<MeshId>(index);
  m_meshes.push_back(mesh);

  return key;
}

auto Scene::get_or_create_sampler(const SamplerCreateInfo &&create_info)
    -> Handle<Sampler> {
  Handle<Sampler> &handle = m_samplers[create_info];
  if (!handle) {
    handle = m_arena.create_sampler(std::move(create_info));
  }
  return handle;
}

auto Scene::get_or_create_texture(ImageId image,
                                  const SamplerDesc &sampler_desc)
    -> SampledTextureId {
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
  m_resource_uploader.stage_texture(*m_renderer, m_frame_arena,
                                    Span((const std::byte *)desc.data, size),
                                    texture);
  auto image = std::bit_cast<ImageId>(u32(m_images.size()));
  m_images.push_back(std::move(texture));
  return image;
}

auto Scene::create_material(const MaterialCreateInfo &desc)
    -> expected<MaterialId> {
  auto get_sampled_texture_id = [&](const auto &texture) -> u32 {
    if (texture.image) {
      return get_or_create_texture(texture.image, texture.sampler);
    }
    return 0;
  };

  auto id = std::bit_cast<MaterialId>(u32(m_materials.size()));
  m_materials.push_back({
      .base_color = desc.base_color_factor,
      .base_color_texture = get_sampled_texture_id(desc.base_color_texture),
      .metallic = desc.metallic_factor,
      .roughness = desc.roughness_factor,
      .metallic_roughness_texture =
          get_sampled_texture_id(desc.metallic_roughness_texture),
      .normal_texture = get_sampled_texture_id(desc.normal_texture),
      .normal_scale = desc.normal_texture.scale,
  });

  return id;
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
  m_pp_opts.exposure.mode = desc.mode;
  m_pp_opts.exposure.ec = desc.ec;
};

auto Scene::create_mesh_instances(
    std::span<const MeshInstanceCreateInfo> create_info,
    std::span<MeshInstanceId> out) -> expected<void> {
  ren_assert(out.size() >= create_info.size());
  for (usize i : range(create_info.size())) {
    ren_assert(create_info[i].mesh);
    ren_assert(create_info[i].material);
    const Mesh &mesh = m_meshes[create_info[i].mesh];
    Handle<MeshInstance> mesh_instance = m_mesh_instances.insert({
        .mesh = create_info[i].mesh,
        .material = create_info[i].material,
        .matrix = glsl::make_decode_position_matrix(mesh.pos_enc_bb),
    });
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
    MeshInstance &mesh_instance =
        m_mesh_instances[std::bit_cast<Handle<MeshInstance>>(
            mesh_instances[i])];
    const Mesh &mesh = m_meshes[mesh_instance.mesh];
    mesh_instance.matrix =
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
      .origin = desc.origin,
  };
};

void Scene::update_rg_config() {
  auto set_if_changed = [&](auto &config, const auto &new_value) {
    if (config != new_value) {
      config = new_value;
      m_rg_valid = false;
    }
  };

  auto grow_if_needed = [&](std::unsigned_integral auto &config,
                            size_t new_size) {
    if (new_size > config) {
      config = new_size * 2;
      m_rg_valid = false;
    }
  };

  m_rg_config.pipelines = &m_pipelines;

  grow_if_needed(m_rg_config.num_meshes, m_meshes.size());
  grow_if_needed(m_rg_config.num_mesh_instances, m_mesh_instances.size());
  grow_if_needed(m_rg_config.num_materials, m_materials.size());
  grow_if_needed(m_rg_config.num_directional_lights, m_dir_lights.size());

  glm::uvec2 viewport = m_swapchain->get_size();
  set_if_changed(m_rg_config.viewport, viewport);

  set_if_changed(m_rg_config.exposure, m_pp_opts.exposure.mode);

#if REN_IMGUI
  {
    ren_ImGuiScope(m_imgui_context);
    set_if_changed(m_rg_config.imgui_context, m_imgui_context);
    if (ImGui::GetCurrentContext()) {
      const ImDrawData *draw_data = ImGui::GetDrawData();
      grow_if_needed(m_rg_config.num_imgui_vertices, draw_data->TotalVtxCount);
      grow_if_needed(m_rg_config.num_imgui_indices, draw_data->TotalIdxCount);
    }
  }
#endif

  if (m_early_z != m_rg_config.early_z) {
    m_rg_config.early_z = m_early_z;
    m_rg_valid = false;
  }
}

auto Scene::draw() -> expected<void> {
  m_pp_opts.exposure.cam_params = m_cameras[m_camera].params;

  m_resource_uploader.upload(*m_renderer, m_cmd_allocator);

  update_rg_config();

  if (not m_rg_valid) {
    RgBuilder rgb(*m_render_graph);
    setup_render_graph(rgb, m_rg_config);
    rgb.build(m_cmd_allocator);
    m_rg_valid = true;
  }

  update_render_graph(
      *m_render_graph, m_rg_config,
      PassesRuntimeConfig{
          .camera = m_cameras[m_camera],
          .index_pools = m_index_pools,
          .meshes = m_meshes,
          .mesh_instances = m_mesh_instances.values(),
          .materials = m_materials,
          .directional_lights = m_dir_lights.values(),

          .pp_opts = m_pp_opts,

          .lod_triangle_pixels = m_lod_triangle_pixels,
          .lod_bias = m_lod_bias,

          .instance_frustum_culling = m_instance_frustum_culling,
          .lod_selection = m_lod_selection,
      });

  m_render_graph->execute(m_cmd_allocator);

  m_frame_arena.clear();

  next_frame();

  return {};
}

#if REN_IMGUI
void Scene::draw_imgui() {
  ren_ImGuiScope(m_imgui_context);
  if (ImGui::GetCurrentContext()) {
    if (ImGui::Begin("Scene renderer settings")) {
      ImGui::SeparatorText("Instance culling");
      {
        bool frustum = m_instance_frustum_culling;
        ImGui::Checkbox("Frustum culling", &frustum);
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
  SampledTextureId texture = get_or_create_texture(image, desc);
  // NOTE: texture from old context is leaked. Don't really care since context
  // will probably be set only once
  io.Fonts->SetTexID((ImTextureID)(uintptr_t)texture);
}
#endif

} // namespace ren
