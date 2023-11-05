#include "Scene.hpp"
#include "Camera.inl"
#include "Formats.hpp"
#include "ImGuiConfig.hpp"
#include "Passes.hpp"
#include "Support/Errors.hpp"
#include "Swapchain.hpp"

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
  u32 num_vertices = desc.positions.size();
  u32 num_indices = desc.indices.size();

  ren_assert(num_vertices > 0);
  ren_assert(desc.normals.size() == num_vertices);
  ren_assert(num_indices > 0 and num_indices % 3 == 0);
  ren_assert_msg(num_vertices <= NUM_VERTEX_POOL_VERTICES,
                 "Vertex pool overflow");
  ren_assert_msg(num_indices <= NUM_VERTEX_POOL_INDICES, "Index pool overflow");

  MeshAttributeFlags attributes;
  if (not desc.tangents.empty()) {
    attributes |= MeshAttribute::Tangent;
  }
  if (not desc.tex_coords.empty()) {
    attributes |= MeshAttribute::UV;
  }
  if (not desc.colors.empty()) {
    attributes |= MeshAttribute::Color;
  }

  // Find or allocate vertex pool

  auto &vertex_pool_list = m_vertex_pool_lists[usize(attributes.get())];
  if (vertex_pool_list.empty()) {
    vertex_pool_list.emplace_back(create_vertex_pool(attributes));
  } else {
    const VertexPool &pool = vertex_pool_list.back();
    if (pool.num_free_indices < num_indices or
        pool.num_free_vertices < num_vertices) {
      vertex_pool_list.emplace_back(create_vertex_pool(attributes));
    }
  }
  ren_assert(not vertex_pool_list.empty());
  u32 vertex_pool_index = vertex_pool_list.size() - 1;
  VertexPool &vertex_pool = vertex_pool_list[vertex_pool_index];

  Mesh mesh = {
      .attributes = attributes,
      .pool = vertex_pool_index,
      .base_vertex = NUM_VERTEX_POOL_VERTICES - vertex_pool.num_free_vertices,
      .base_index = NUM_VERTEX_POOL_INDICES - vertex_pool.num_free_indices,
      .num_indices = num_indices,
  };

  vertex_pool.num_free_vertices -= num_vertices;
  vertex_pool.num_free_indices -= num_indices;

  // Upload vertices

  {
    mesh.bb = ranges::accumulate(
        desc.positions |
            map([](const glm::vec3 &pos) { return glm::abs(pos); }),
        glm::vec3(0.0f),
        [](const glm::vec3 &l, const glm::vec3 &r) { return glm::max(l, r); });
    mesh.bb = glm::exp2(glm::ceil(glm::log2(mesh.bb)));

    Vector<glsl::Position> positions =
        desc.positions | map([&](const glm::vec3 &position) {
          return glsl::encode_position(position, mesh.bb);
        });

    auto positions_dst =
        g_renderer->get_buffer_view(vertex_pool.positions)
            .slice<glsl::Position>(mesh.base_vertex, num_vertices);

    m_resource_uploader.stage_buffer(m_frame_arena, Span(positions),
                                     positions_dst);

    glm::mat3 normal_matrix = glsl::make_decode_position_matrix(mesh.bb);

    Vector<glm::vec3> normals =
        desc.normals |
        map([&](const glm::vec3 &normal) { return normal_matrix * normal; });

    auto normals_dst = g_renderer->get_buffer_view(vertex_pool.normals)
                           .slice<glm::vec3>(mesh.base_vertex, num_vertices);
    m_resource_uploader.stage_buffer(m_frame_arena, Span(normals), normals_dst);
  }
  if (not desc.tangents.empty()) {
    glm::mat3 normal_matrix = glsl::make_decode_position_matrix(mesh.bb);

    Vector<glm::vec4> tangents =
        desc.tangents | map([&](const glm::vec4 &tangent) {
          return glm::vec4(normal_matrix * glm::vec3(tangent), tangent.w);
        });

    auto tangents_dst = g_renderer->get_buffer_view(vertex_pool.tangents)
                            .slice<glm::vec4>(mesh.base_vertex, num_vertices);
    m_resource_uploader.stage_buffer(m_frame_arena, Span(tangents),
                                     tangents_dst);
  }
  if (not desc.tex_coords.empty()) {
    auto uvs_dst = g_renderer->get_buffer_view(vertex_pool.uvs)
                       .slice<glm::vec2>(mesh.base_vertex, num_vertices);
    m_resource_uploader.stage_buffer(m_frame_arena, Span(desc.tex_coords),
                                     uvs_dst);
  }
  if (not desc.colors.empty()) {
    auto colors_dst = g_renderer->get_buffer_view(vertex_pool.colors)
                          .slice<glm::vec4>(mesh.base_vertex, num_vertices);
    m_resource_uploader.stage_buffer(m_frame_arena, Span(desc.colors),
                                     colors_dst);
  }
  {
    auto indices_dst = g_renderer->get_buffer_view(vertex_pool.indices)
                           .slice<u32>(mesh.base_index, num_indices);
    m_resource_uploader.stage_buffer(m_frame_arena, Span(desc.indices),
                                     indices_dst);
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
          .matrix = glsl::make_decode_position_matrix(mesh.bb),
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
          .matrix = transform * glsl::make_decode_position_matrix(mesh.bb),
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
    mesh_instance.matrix = matrix * glsl::make_decode_position_matrix(mesh.bb);
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

void SceneImpl::draw() {
  m_resource_uploader.upload(m_cmd_allocator);

  update_rg_passes(
      *m_render_graph, m_cmd_allocator,
      PassesConfig {
#if REN_IMGUI
        .imgui_context = m_imgui_context,
#endif
        .pipelines = &m_pipelines,
        .viewport = {m_viewport_width, m_viewport_height},
        .pp_opts = &m_pp_opts, .early_z = m_early_z,
      },
      PassesData{
          .vertex_pool_lists = m_vertex_pool_lists,
          .meshes = m_meshes,
          .materials = m_materials,
          .mesh_instances = m_mesh_instances.values(),
          .directional_lights = m_dir_lights.values(),
          .viewport_size = {m_viewport_width, m_viewport_height},
          .camera = &m_camera,
          .pp_opts = &m_pp_opts,
      });

  m_render_graph->execute(m_cmd_allocator);

  m_frame_arena.clear();
}

#if REN_IMGUI
void SceneImpl::draw_imgui() {
  ren_ImGuiScope(m_imgui_context);
  if (ImGui::GetCurrentContext()) {
    if (ImGui::Begin("Scene renderer settings")) {
      bool early_z = m_early_z;
      ImGui::Checkbox("Early Z", &early_z);
      m_early_z = early_z;
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
