#include "Scene.hpp"
#include "Camera.inl"
#include "Formats.hpp"
#include "ImGuiConfig.hpp"
#include "MeshProcessing.hpp"
#include "Passes.hpp"
#include "Support/Errors.hpp"
#include "Swapchain.hpp"
#include "glsl/Batch.hpp"

#include <range/v3/algorithm.hpp>
#include <range/v3/numeric.hpp>
#include <range/v3/range.hpp>

namespace ren {

auto Hash<SamplerDesc>::operator()(const SamplerDesc &sampler) const noexcept
    -> usize {
  usize seed = 0;
  seed = hash_combine(seed, sampler.mag_filter);
  seed = hash_combine(seed, sampler.min_filter);
  seed = hash_combine(seed, sampler.mipmap_filter);
  seed = hash_combine(seed, sampler.wrap_u);
  seed = hash_combine(seed, sampler.wrap_v);
  return seed;
}

SceneImpl::SceneImpl(SwapchainImpl &swapchain) {
  m_persistent_descriptor_set_layout =
      create_persistent_descriptor_set_layout();
  std::tie(m_persistent_descriptor_pool, m_persistent_descriptor_set) =
      allocate_descriptor_pool_and_set(m_persistent_descriptor_set_layout);

  m_texture_allocator = std::make_unique<TextureIdAllocator>(
      m_persistent_descriptor_set, m_persistent_descriptor_set_layout);

  m_render_graph =
      std::make_unique<RenderGraph>(swapchain, *m_texture_allocator);

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

void SceneImpl::next_frame() {
  m_cmd_allocator.next_frame();
  m_texture_allocator->next_frame();
}

auto SceneImpl::create_mesh(const MeshDesc &desc) -> MeshId {
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
      .uvs = desc.tex_coords,
      .colors = desc.colors,
      .enc_positions = &positions,
      .enc_normals = &normals,
      .enc_tangents = &tangents,
      .enc_uvs = &uvs,
      .enc_colors = &colors,
      .indices = &indices,
  });

  u32 num_vertices = positions.size();

  ren_assert_msg(num_vertices <= glsl::NUM_VERTEX_POOL_VERTICES,
                 "Vertex pool overflow");
  ren_assert_msg(mesh.num_indices <= glsl::NUM_VERTEX_POOL_INDICES,
                 "Index pool overflow");

  // Find or allocate vertex pool

  auto &vertex_pool_list = m_vertex_pool_lists[usize(mesh.attributes.get())];
  if (vertex_pool_list.empty()) {
    vertex_pool_list.emplace_back(create_vertex_pool(mesh.attributes));
  } else {
    const VertexPool &pool = vertex_pool_list.back();
    if (pool.num_free_indices < mesh.num_indices or
        pool.num_free_vertices < num_vertices) {
      vertex_pool_list.emplace_back(create_vertex_pool(mesh.attributes));
    }
  }
  ren_assert(not vertex_pool_list.empty());
  mesh.pool = vertex_pool_list.size() - 1;
  VertexPool &vertex_pool = vertex_pool_list[mesh.pool];

  mesh.base_vertex =
      glsl::NUM_VERTEX_POOL_VERTICES - vertex_pool.num_free_vertices;
  mesh.base_index =
      glsl::NUM_VERTEX_POOL_INDICES - vertex_pool.num_free_indices;
  for (glsl::MeshLOD &lod : mesh.lods) {
    lod.base_index += mesh.base_index;
  }

  vertex_pool.num_free_vertices -= num_vertices;
  vertex_pool.num_free_indices -= mesh.num_indices;

  // Upload vertices

  auto stage_stream = [&]<typename T>(const Vector<T> &stream,
                                      Handle<Buffer> buffer) {
    if (not stream.empty()) {
      auto stream_dst = g_renderer->get_buffer_view(buffer).slice<T>(
          mesh.base_vertex, num_vertices);
      m_resource_uploader.stage_buffer(m_frame_arena, Span(stream), stream_dst);
    }
  };

  stage_stream(positions, vertex_pool.positions);
  stage_stream(normals, vertex_pool.normals);
  stage_stream(tangents, vertex_pool.tangents);
  stage_stream(uvs, vertex_pool.uvs);
  stage_stream(colors, vertex_pool.colors);

  {
    auto indices_dst = g_renderer->get_buffer_view(vertex_pool.indices)
                           .slice<u32>(mesh.base_index, mesh.num_indices);
    m_resource_uploader.stage_buffer(m_frame_arena, Span(indices), indices_dst);
  }

  auto key = std::bit_cast<MeshId>(u32(m_meshes.size()));
  m_meshes.push_back(mesh);

  return key;
}

auto SceneImpl::get_or_create_sampler(const SamplerDesc &sampler)
    -> Handle<Sampler> {
  AutoHandle<Sampler> &handle = m_samplers[sampler];
  if (!handle) {
    handle = g_renderer->create_sampler({
        .mag_filter = getVkFilter(sampler.mag_filter),
        .min_filter = getVkFilter(sampler.min_filter),
        .mipmap_mode = getVkSamplerMipmapMode(sampler.mipmap_filter),
        .address_mode_u = getVkSamplerAddressMode(sampler.wrap_u),
        .address_mode_v = getVkSamplerAddressMode(sampler.wrap_v),
        .anisotropy = 16.0f,
    });
  }
  return handle;
}

auto SceneImpl::get_or_create_texture(ImageId image,
                                      const SamplerDesc &sampler_desc)
    -> SampledTextureId {
  auto view = g_renderer->get_texture_view(m_images[image]);
  auto sampler = get_or_create_sampler(sampler_desc);
  return m_texture_allocator->allocate_sampled_texture(view, sampler);
}

auto SceneImpl::create_image(const ImageDesc &desc) -> ImageId {
  auto format = getVkFormat(desc.format);
  auto texture = g_renderer->create_texture({
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
      m_frame_arena, Span((const std::byte *)desc.data, size), texture);
  auto image = std::bit_cast<ImageId>(u32(m_images.size()));
  m_images.push_back(std::move(texture));
  return image;
}

void SceneImpl::create_materials(Span<const MaterialDesc> descs,
                                 MaterialId *out) {
  for (const auto &desc : descs) {
    glsl::Material material = {
        .base_color = desc.base_color_factor,
        .base_color_texture = [&]() -> u32 {
          if (desc.base_color_texture.image) {
            return get_or_create_texture(desc.base_color_texture.image,
                                         desc.base_color_texture.sampler);
          }
          return 0;
        }(),
        .metallic = desc.metallic_factor,
        .roughness = desc.roughness_factor,
        .metallic_roughness_texture = [&]() -> u32 {
          if (desc.metallic_roughness_texture.image) {
            return get_or_create_texture(
                desc.metallic_roughness_texture.image,
                desc.metallic_roughness_texture.sampler);
          }
          return 0;
        }(),
        .normal_texture = [&]() -> u32 {
          if (desc.normal_texture.image) {
            return get_or_create_texture(desc.normal_texture.image,
                                         desc.normal_texture.sampler);
          }
          return 0;
        }(),
        .normal_scale = desc.normal_texture.scale,
    };

    auto index = std::bit_cast<MaterialId>(u32(m_materials.size()));
    m_materials.push_back(material);

    *out = index;
    ++out;
  }
}

void SceneImpl::set_camera(const CameraDesc &desc) noexcept {
  m_camera = Camera{
      .position = desc.position,
      .forward = glm::normalize(desc.forward),
      .up = glm::normalize(desc.up),
      .projection = desc.projection,
  };

  m_pp_opts.exposure = {
      .mode = [&]() -> ExposureOptions::Mode {
        switch (desc.exposure_mode) {
        default:
          unreachable("Unknown exposure mode");
        case ExposureMode::Camera:
          return ExposureOptions::Camera{
              .aperture = desc.aperture,
              .shutter_time = desc.shutter_time,
              .iso = desc.iso,
              .exposure_compensation = desc.exposure_compensation,
          };
        case ExposureMode::Automatic:
          return ExposureOptions::Automatic{
              .exposure_compensation = desc.exposure_compensation,
          };
        }
      }(),
  };

  m_viewport_width = desc.width;
  m_viewport_height = desc.height;
}

void SceneImpl::set_tone_mapping(const ToneMappingDesc &oper) noexcept {
  m_pp_opts.tone_mapping = {
      .oper = oper,
  };
};

void SceneImpl::create_mesh_instances(Span<const MeshInstanceDesc> descs,
                                      Span<const glm::mat4x3> transforms,
                                      MeshInstanceId *out) {
  if (transforms.empty()) {
    for (const MeshInstanceDesc &desc : descs) {
      ren_assert(desc.mesh);
      ren_assert(desc.material);
      const Mesh &mesh = m_meshes[desc.mesh];
      Handle<MeshInstance> mesh_instance = m_mesh_instances.insert({
          .mesh = desc.mesh,
          .material = desc.material,
          .matrix = glsl::make_decode_position_matrix(mesh.pos_enc_bb),
      });
      *out = std::bit_cast<MeshInstanceId>(mesh_instance);
      ++out;
    }
  } else {
    ren_assert(descs.size() == transforms.size());
    for (const auto &[desc, transform] : zip(descs, transforms)) {
      ren_assert(desc.mesh);
      ren_assert(desc.material);
      const Mesh &mesh = m_meshes[desc.mesh];
      Handle<MeshInstance> mesh_instance = m_mesh_instances.insert({
          .mesh = desc.mesh,
          .material = desc.material,
          .matrix =
              transform * glsl::make_decode_position_matrix(mesh.pos_enc_bb),
      });
      *out = std::bit_cast<MeshInstanceId>(mesh_instance);
      ++out;
    }
  }
}

void SceneImpl::destroy_mesh_instances(
    Span<const MeshInstanceId> mesh_instances) noexcept {
  for (MeshInstanceId mesh_instance : mesh_instances) {
    m_mesh_instances.erase(std::bit_cast<Handle<MeshInstance>>(mesh_instance));
  }
}

void SceneImpl::set_mesh_instance_transforms(
    Span<const MeshInstanceId> mesh_instances,
    Span<const glm::mat4x3> matrices) noexcept {
  ren_assert(mesh_instances.size() == matrices.size());
  for (const auto &[handle, matrix] : zip(mesh_instances, matrices)) {
    MeshInstance &mesh_instance =
        m_mesh_instances[std::bit_cast<Handle<MeshInstance>>(handle)];
    const Mesh &mesh = m_meshes[mesh_instance.mesh];
    mesh_instance.matrix =
        matrix * glsl::make_decode_position_matrix(mesh.pos_enc_bb);
  }
}

auto SceneImpl::create_directional_light(const DirectionalLightDesc &desc)
    -> DirectionalLightId {
  auto light = m_dir_lights.insert(glsl::DirLight{
      .color = desc.color,
      .illuminance = desc.illuminance,
      .origin = desc.origin,
  });
  return std::bit_cast<DirectionalLightId>(light);
};

void SceneImpl::destroy_directional_light(DirectionalLightId light) noexcept {
  m_dir_lights.erase(std::bit_cast<Handle<glsl::DirLight>>(light));
}

void SceneImpl::update_directional_light(
    DirectionalLightId light, const DirectionalLightDesc &desc) noexcept {
  m_dir_lights[std::bit_cast<Handle<glsl::DirLight>>(light)] = {
      .color = desc.color,
      .illuminance = desc.illuminance,
      .origin = desc.origin,
  };
};

void SceneImpl::update_rg_config() {
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

  glm::uvec2 viewport = {m_viewport_width, m_viewport_height};
  set_if_changed(m_rg_config.viewport, viewport);

  auto exposure_mode = m_pp_opts.exposure.mode.visit(OverloadSet{
      [](const ExposureOptions::Manual &) -> ExposureMode { todo(); },
      [](const ExposureOptions::Camera &) { return ExposureMode::Camera; },
      [](const ExposureOptions::Automatic &) {
        return ExposureMode::Automatic;
      },
  });
  set_if_changed(m_rg_config.exposure_mode, exposure_mode);

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

void SceneImpl::draw() {
  m_resource_uploader.upload(m_cmd_allocator);

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
          .camera = m_camera,
          .vertex_pool_lists = m_vertex_pool_lists,
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
}

#if REN_IMGUI
void SceneImpl::draw_imgui() {
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

void SceneImpl::set_imgui_context(ImGuiContext *context) noexcept {
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
  });
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
