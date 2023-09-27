#include "Scene.hpp"
#include "Camera.inl"
#include "Formats.hpp"
#include "Passes.hpp"
#include "Support/Errors.hpp"
#include "Support/Span.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <range/v3/algorithm.hpp>
#include <range/v3/numeric.hpp>
#include <range/v3/range.hpp>

namespace ren {

auto Hash<RenSampler>::operator()(const RenSampler &sampler) const noexcept
    -> usize {
  usize seed = 0;
  seed = hash_combine(seed, sampler.mag_filter);
  seed = hash_combine(seed, sampler.min_filter);
  seed = hash_combine(seed, sampler.mipmap_filter);
  seed = hash_combine(seed, sampler.wrap_u);
  seed = hash_combine(seed, sampler.wrap_v);
  return seed;
}

constexpr usize MESH_VERTEX_BUDGET = 1024 * 1024;
constexpr usize MESH_INDEX_BUDGET = 1024 * 1024;

Scene::Scene(Swapchain &swapchain) {
  m_persistent_descriptor_set_layout =
      create_persistent_descriptor_set_layout(m_persistent_arena);
  std::tie(m_persistent_descriptor_pool, m_persistent_descriptor_set) =
      allocate_descriptor_pool_and_set(m_persistent_arena,
                                       m_persistent_descriptor_set_layout);

  m_texture_allocator = std::make_unique<TextureIdAllocator>(
      m_persistent_descriptor_set, m_persistent_descriptor_set_layout);

  m_render_graph =
      std::make_unique<RenderGraph>(swapchain, *m_texture_allocator);

  m_pipelines =
      load_pipelines(m_persistent_arena, m_persistent_descriptor_set_layout);

  m_vertex_positions = m_persistent_arena.create_buffer({
      .name = "Mesh vertex positions pool",
      .heap = BufferHeap::Static,
      .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      .size = sizeof(glm::vec3) * MESH_VERTEX_BUDGET,
  });

  m_vertex_normals = m_persistent_arena.create_buffer({
      .name = "Mesh vertex normals pool",
      .heap = BufferHeap::Static,
      .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      .size = sizeof(glm::vec3) * MESH_VERTEX_BUDGET,
  });

  m_vertex_tangents = m_persistent_arena.create_buffer({
      .name = "Mesh vertex tangents pool",
      .heap = BufferHeap::Static,
      .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      .size = sizeof(glm::vec4) * MESH_VERTEX_BUDGET,
  });

  m_vertex_colors = m_persistent_arena.create_buffer({
      .name = "Mesh vertex colors pool",
      .heap = BufferHeap::Static,
      .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      .size = sizeof(glm::vec4) * MESH_VERTEX_BUDGET,
  });

  m_vertex_uvs = m_persistent_arena.create_buffer({
      .name = "Mesh vertex UVs pool",
      .heap = BufferHeap::Static,
      .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      .size = sizeof(glm::vec2) * MESH_VERTEX_BUDGET,
  });

  m_vertex_indices = m_persistent_arena.create_buffer({
      .name = "Mesh vertex indices pool",
      .heap = BufferHeap::Static,
      .usage =
          VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
      .size = sizeof(u32) * MESH_INDEX_BUDGET,
  });

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
  m_cmd_allocator.next_frame();
  m_texture_allocator->next_frame();
}

RenMesh Scene::create_mesh(const RenMeshDesc &desc) {
  ren_assert(desc.positions);
  ren_assert(desc.normals);
  ren_assert(desc.indices);

  // Create mesh

  Mesh mesh = {};

  mesh.base_vertex = m_num_vertex_positions;
  m_num_vertex_positions += desc.num_vertices;
  ren_assert_msg(m_num_vertex_positions <= MESH_VERTEX_BUDGET,
                 "Mesh vertex positions pool overflow");

  if (desc.tangents) {
    mesh.base_tangent_vertex = m_num_vertex_tangents;
    m_num_vertex_tangents += desc.num_vertices;
    ren_assert_msg(m_num_vertex_tangents <= MESH_VERTEX_BUDGET,
                   "Mesh vertex tangents pool overflow");
  }

  if (desc.colors) {
    mesh.base_color_vertex = m_num_vertex_colors;
    m_num_vertex_colors += desc.num_vertices;
    ren_assert_msg(m_num_vertex_colors <= MESH_VERTEX_BUDGET,
                   "Mesh vertex colors pool overflow");
  }

  if (desc.uvs) {
    mesh.base_uv_vertex = m_num_vertex_uvs;
    m_num_vertex_uvs += desc.num_vertices;
    ren_assert_msg(m_num_vertex_uvs <= MESH_VERTEX_BUDGET,
                   "Mesh vertex UVs pool overflow");
  }

  mesh.base_index = m_num_vertex_indices;
  mesh.num_indices = desc.num_indices;
  m_num_vertex_indices += desc.num_indices;
  ren_assert_msg(m_num_vertex_indices <= MESH_INDEX_BUDGET,
                 "Mesh vertex index pool overflow");

  // Upload vertices

  {
    auto positions_src =
        Span(desc.positions, desc.num_vertices).reinterpret<glm::vec3>();
    auto positions_dst = m_vertex_positions.slice<glm::vec3>(mesh.base_vertex,
                                                             desc.num_vertices);
    m_resource_uploader.stage_buffer(m_frame_arena, positions_src,
                                     positions_dst);
    auto normals_src =
        Span(desc.normals, desc.num_vertices).reinterpret<glm::vec3>();
    auto normals_dst =
        m_vertex_normals.slice<glm::vec3>(mesh.base_vertex, desc.num_vertices);
    m_resource_uploader.stage_buffer(m_frame_arena, normals_src, normals_dst);
  }
  if (mesh.base_tangent_vertex != glsl::MESH_ATTRIBUTE_UNUSED) {
    auto tangents_src =
        Span(desc.tangents, desc.num_vertices).reinterpret<glm::vec4>();
    auto tangents_dst = m_vertex_tangents.slice<glm::vec4>(
        mesh.base_tangent_vertex, desc.num_vertices);
    m_resource_uploader.stage_buffer(m_frame_arena, tangents_src, tangents_dst);
  }
  if (mesh.base_color_vertex != glsl::MESH_ATTRIBUTE_UNUSED) {
    auto colors_src =
        Span(desc.colors, desc.num_vertices).reinterpret<glm::vec4>();
    auto colors_dst = m_vertex_colors.slice<glm::vec4>(mesh.base_color_vertex,
                                                       desc.num_vertices);
    m_resource_uploader.stage_buffer(m_frame_arena, colors_src, colors_dst);
  }
  if (mesh.base_uv_vertex != glsl::MESH_ATTRIBUTE_UNUSED) {
    auto uvs_src = Span(desc.uvs, desc.num_vertices).reinterpret<glm::vec2>();
    auto uvs_dst =
        m_vertex_uvs.slice<glm::vec2>(mesh.base_uv_vertex, desc.num_vertices);
    m_resource_uploader.stage_buffer(m_frame_arena, uvs_src, uvs_dst);
  }
  {
    Span<const u32> indices_src(desc.indices, desc.num_indices);
    auto indices_dst =
        m_vertex_indices.slice<u32>(mesh.base_index, mesh.num_indices);
    m_resource_uploader.stage_buffer(m_frame_arena, indices_src, indices_dst);
  }

  auto key = static_cast<RenMesh>(m_meshes.size());
  m_meshes.push_back(mesh);

  return key;
}

auto Scene::get_or_create_sampler(const RenSampler &sampler)
    -> Handle<Sampler> {
  Handle<Sampler> &handle = m_samplers[sampler];
  if (!handle) {
    handle = m_persistent_arena.create_sampler({
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

auto Scene::get_or_create_texture(const RenTexture &texture)
    -> SampledTextureId {
  auto view = g_renderer->get_texture_view(m_images[texture.image]);
  view.swizzle = getTextureSwizzle(texture.swizzle);
  auto sampler = get_or_create_sampler(texture.sampler);
  return m_texture_allocator->allocate_sampled_texture(view, sampler);
}

auto Scene::create_image(const RenImageDesc &desc) -> RenImage {
  auto image = static_cast<RenImage>(m_images.size());
  auto format = getVkFormat(desc.format);
  auto texture = m_persistent_arena.create_texture({
      .type = VK_IMAGE_TYPE_2D,
      .format = format,
      .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
               VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
      .width = desc.width,
      .height = desc.height,
      .num_mip_levels = get_mip_level_count(desc.width, desc.height),
  });
  m_images.push_back(texture);
  auto image_size = desc.width * desc.height * get_format_size(format);
  m_resource_uploader.stage_texture(
      m_frame_arena,
      std::span(reinterpret_cast<const std::byte *>(desc.data), image_size),
      texture);
  return image;
}

void Scene::create_materials(std::span<const RenMaterialDesc> descs,
                             RenMaterial *out) {
  for (const auto &desc : descs) {
    glsl::Material material = {
        .base_color = glm::make_vec4(desc.base_color_factor),
        .base_color_texture = [&]() -> u32 {
          if (desc.color_tex.image) {
            return get_or_create_texture(desc.color_tex);
          }
          return 0;
        }(),
        .metallic = desc.metallic_factor,
        .roughness = desc.roughness_factor,
        .metallic_roughness_texture = [&]() -> u32 {
          if (desc.metallic_roughness_tex.image) {
            return get_or_create_texture(desc.metallic_roughness_tex);
          }
          return 0;
        }(),
        .normal_texture = [&]() -> u32 {
          if (desc.normal_tex.image) {
            return get_or_create_texture(desc.normal_tex);
          }
          return 0;
        }(),
        .normal_scale = desc.normal_scale,
    };

    auto index = static_cast<RenMaterial>(m_materials.size());
    m_materials.push_back(material);

    *out = index;
    ++out;
  }
}

void Scene::set_camera(const RenCameraDesc &desc) noexcept {
  m_camera = {
      .position = glm::make_vec3(desc.position),
      .forward = glm::normalize(glm::make_vec3(desc.forward)),
      .up = glm::normalize(glm::make_vec3(desc.up)),
      .projection = [&]() -> CameraProjection {
        switch (desc.projection) {
        case REN_PROJECTION_PERSPECTIVE:
          return desc.perspective;
        case REN_PROJECTION_ORTHOGRAPHIC:
          return desc.orthographic;
        }
        unreachable("Unknown projection");
      }(),
  };

  m_pp_opts.exposure = {
      .mode = [&]() -> ExposureOptions::Mode {
        switch (desc.exposure_mode) {
        case REN_EXPOSURE_MODE_CAMERA:
          return ExposureOptions::Camera{
              .aperture = desc.aperture,
              .shutter_time = desc.shutter_time,
              .iso = desc.iso,
              .exposure_compensation = desc.exposure_compensation,
          };
        case REN_EXPOSURE_MODE_AUTOMATIC:
          return ExposureOptions::Automatic{
              .exposure_compensation = desc.exposure_compensation,
          };
        }
        unreachable("Unknown exposure mode");
      }(),
  };

  m_viewport_width = desc.width;
  m_viewport_height = desc.height;
}

void Scene::set_tone_mapping(RenToneMappingOperator oper) noexcept {
  m_pp_opts.tone_mapping = {
      .oper = oper,
  };
};

void Scene::create_mesh_insts(std::span<const RenMeshInstDesc> descs,
                              RenMeshInst *out) {
  for (const auto &desc : descs) {
    ren_assert(desc.mesh);
    ren_assert(desc.material);
    // TODO: return proper error
    const Mesh &mesh = m_meshes[desc.mesh];
    const glsl::Material &material = m_materials[desc.material];
    if (material.base_color_texture) {
      ren_assert_msg(
          mesh.base_uv_vertex != glsl::MESH_ATTRIBUTE_UNUSED,
          "Mesh instance material with base color texture requires mesh "
          "with UVs");
    }
    if (material.metallic_roughness_texture) {
      ren_assert_msg(mesh.base_uv_vertex != glsl::MESH_ATTRIBUTE_UNUSED,
                     "Mesh instance material with metallic-roughness texture "
                     "requires mesh with UVs");
    }
    if (material.normal_texture) {
      ren_assert_msg(
          mesh.base_tangent_vertex != glsl::MESH_ATTRIBUTE_UNUSED,
          "Mesh instance material with normal map requires mesh with tangents");
      ren_assert_msg(
          mesh.base_uv_vertex != glsl::MESH_ATTRIBUTE_UNUSED,
          "Mesh instance material with normal map requires mesh with UVs");
    }
    auto mesh_inst = m_mesh_instances.insert({
        .mesh = desc.mesh,
        .material = desc.material,
        .matrix = glm::mat4(1.0f),
    });
    *out = std::bit_cast<RenMeshInst>(mesh_inst);
    ++out;
  }
}

void Scene::destroy_mesh_insts(
    std::span<const RenMeshInst> mesh_insts) noexcept {
  for (auto mesh_inst : mesh_insts) {
    m_mesh_instances.erase(std::bit_cast<Handle<MeshInstance>>(mesh_inst));
  }
}

void Scene::set_mesh_inst_matrices(
    std::span<const RenMeshInst> mesh_insts,
    std::span<const RenMatrix4x4> matrices) noexcept {
  for (const auto &[mesh_inst, matrix] : zip(mesh_insts, matrices)) {
    m_mesh_instances[std::bit_cast<Handle<MeshInstance>>(mesh_inst)].matrix =
        glm::make_mat4(matrix[0]);
  }
}

void Scene::create_dir_lights(std::span<const RenDirLightDesc> descs,
                              RenDirLight *out) {
  for (const auto &desc : descs) {
    auto light = m_dir_lights.insert(glsl::DirLight{
        .color = glm::make_vec3(desc.color),
        .illuminance = desc.illuminance,
        .origin = glm::make_vec3(desc.origin),
    });
    *out = std::bit_cast<RenDirLight>(light);
    ++out;
  }
};

void Scene::destroy_dir_lights(std::span<const RenDirLight> lights) noexcept {
  for (auto light : lights) {
    m_dir_lights.erase(std::bit_cast<Handle<glsl::DirLight>>(light));
  }
}

void Scene::config_dir_lights(std::span<const RenDirLight> lights,
                              std::span<const RenDirLightDesc> descs) {
  for (const auto &[light, desc] : zip(lights, descs)) {
    m_dir_lights[std::bit_cast<Handle<glsl::DirLight>>(light)] = {
        .color = glm::make_vec3(desc.color),
        .illuminance = desc.illuminance,
        .origin = glm::make_vec3(desc.origin),
    };
  }
}

void Scene::draw() {
  m_resource_uploader.upload(m_cmd_allocator);

  update_rg_passes(*m_render_graph, m_cmd_allocator,
                   PassesConfig{
                       .pipelines = &m_pipelines,
                       .viewport_size = {m_viewport_width, m_viewport_height},
                       .pp_opts = &m_pp_opts,
                   },
                   PassesData{
                       .vertex_positions = m_vertex_positions,
                       .vertex_normals = m_vertex_normals,
                       .vertex_tangents = m_vertex_tangents,
                       .vertex_colors = m_vertex_colors,
                       .vertex_uvs = m_vertex_uvs,
                       .vertex_indices = m_vertex_indices,
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

} // namespace ren
