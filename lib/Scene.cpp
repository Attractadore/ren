#include "Scene.hpp"
#include "Camera.inl"
#include "Formats.hpp"
#include "Passes.hpp"
#include "Support/Errors.hpp"
#include "Swapchain.hpp"

#include <range/v3/algorithm.hpp>
#include <range/v3/numeric.hpp>
#include <range/v3/range.hpp>

#if REN_IMGUI
#include <imgui.h>
#include <imgui_internal.h>
#endif

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

constexpr usize MESH_VERTEX_BUDGET = 1024 * 1024;
constexpr usize MESH_INDEX_BUDGET = 1024 * 1024;

SceneImpl::SceneImpl(SwapchainImpl &swapchain) {
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

void SceneImpl::next_frame() {
  m_cmd_allocator.next_frame();
  m_texture_allocator->next_frame();
}

auto SceneImpl::create_mesh(const MeshDesc &desc) -> MeshId {
  usize num_vertices = desc.positions.size();
  usize num_indices = desc.indices.size();

  ren_assert(num_vertices > 0);
  ren_assert(desc.normals.size() == num_vertices);
  ren_assert(num_indices > 0 and num_indices % 3 == 0);

  // Create mesh

  Mesh mesh = {};

  mesh.base_vertex = m_num_vertex_positions;
  m_num_vertex_positions += num_vertices;
  ren_assert_msg(m_num_vertex_positions <= MESH_VERTEX_BUDGET,
                 "Mesh vertex positions pool overflow");

  if (not desc.tangents.empty()) {
    ren_assert(desc.tangents.size() == num_vertices);
    mesh.base_tangent_vertex = m_num_vertex_tangents;
    m_num_vertex_tangents += num_vertices;
    ren_assert_msg(m_num_vertex_tangents <= MESH_VERTEX_BUDGET,
                   "Mesh vertex tangents pool overflow");
  }

  if (not desc.colors.empty()) {
    mesh.base_color_vertex = m_num_vertex_colors;
    m_num_vertex_colors += num_vertices;
    ren_assert_msg(m_num_vertex_colors <= MESH_VERTEX_BUDGET,
                   "Mesh vertex colors pool overflow");
  }

  if (not desc.tex_coords.empty()) {
    mesh.base_uv_vertex = m_num_vertex_uvs;
    m_num_vertex_uvs += num_vertices;
    ren_assert_msg(m_num_vertex_uvs <= MESH_VERTEX_BUDGET,
                   "Mesh vertex UVs pool overflow");
  }

  mesh.base_index = m_num_vertex_indices;
  mesh.num_indices = num_indices;
  m_num_vertex_indices += num_indices;
  ren_assert_msg(m_num_vertex_indices <= MESH_INDEX_BUDGET,
                 "Mesh vertex index pool overflow");

  // Upload vertices

  {
    auto positions_dst =
        m_vertex_positions.slice<glm::vec3>(mesh.base_vertex, num_vertices);
    m_resource_uploader.stage_buffer(m_frame_arena, desc.positions,
                                     positions_dst);
    auto normals_dst =
        m_vertex_normals.slice<glm::vec3>(mesh.base_vertex, num_vertices);
    m_resource_uploader.stage_buffer(m_frame_arena, desc.normals, normals_dst);
  }
  if (mesh.base_tangent_vertex != glsl::MESH_ATTRIBUTE_UNUSED) {
    auto tangents_dst = m_vertex_tangents.slice<glm::vec4>(
        mesh.base_tangent_vertex, num_vertices);
    m_resource_uploader.stage_buffer(m_frame_arena, desc.tangents,
                                     tangents_dst);
  }
  if (mesh.base_color_vertex != glsl::MESH_ATTRIBUTE_UNUSED) {
    auto colors_dst =
        m_vertex_colors.slice<glm::vec4>(mesh.base_color_vertex, num_vertices);
    m_resource_uploader.stage_buffer(m_frame_arena, desc.colors, colors_dst);
  }
  if (mesh.base_uv_vertex != glsl::MESH_ATTRIBUTE_UNUSED) {
    auto uvs_dst =
        m_vertex_uvs.slice<glm::vec2>(mesh.base_uv_vertex, num_vertices);
    m_resource_uploader.stage_buffer(m_frame_arena, desc.tex_coords, uvs_dst);
  }
  {
    auto indices_dst =
        m_vertex_indices.slice<u32>(mesh.base_index, num_indices);
    m_resource_uploader.stage_buffer(m_frame_arena, desc.indices, indices_dst);
  }

  auto key = std::bit_cast<MeshId>(u32(m_meshes.size()));
  m_meshes.push_back(mesh);

  return key;
}

auto SceneImpl::get_or_create_sampler(const SamplerDesc &sampler)
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

auto SceneImpl::get_or_create_texture(ImageId image,
                                      const SamplerDesc &sampler_desc)
    -> SampledTextureId {
  auto view = g_renderer->get_texture_view(m_images[image]);
  auto sampler = get_or_create_sampler(sampler_desc);
  return m_texture_allocator->allocate_sampled_texture(view, sampler);
}

auto SceneImpl::create_image(const ImageDesc &desc) -> ImageId {
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
  usize size = desc.width * desc.height * get_format_size(format);
  m_resource_uploader.stage_texture(
      m_frame_arena, Span((const std::byte *)desc.data, size), texture);
  auto image = std::bit_cast<ImageId>(u32(m_images.size()));
  m_images.push_back(texture);
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
  auto verify_desc = [&](const MeshInstanceDesc &desc) {
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
  };

  if (transforms.empty()) {
    for (const MeshInstanceDesc &desc : descs) {
      verify_desc(desc);
      Handle<MeshInstance> mesh_instance = m_mesh_instances.insert({
          .mesh = desc.mesh,
          .material = desc.material,
      });
      *out = std::bit_cast<MeshInstanceId>(mesh_instance);
      ++out;
    }
  } else {
    ren_assert(descs.size() == transforms.size());
    for (const auto &[desc, transform] : zip(descs, transforms)) {
      verify_desc(desc);
      Handle<MeshInstance> mesh_instance = m_mesh_instances.insert({
          .mesh = desc.mesh,
          .material = desc.material,
          .matrix = transform,
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
  for (const auto &[mesh_instance, matrix] : zip(mesh_instances, matrices)) {
    m_mesh_instances[std::bit_cast<Handle<MeshInstance>>(mesh_instance)]
        .matrix = matrix;
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

  update_rg_passes(*m_render_graph, m_cmd_allocator,
                   PassesConfig{
                       .pipelines = &m_pipelines,
                       .viewport_size = {m_viewport_width, m_viewport_height},
                       .pp_opts = &m_pp_opts,
                       .early_z = false,
                       .imgui = m_imgui_context and m_imgui_enabled,
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
                       .imgui_context = m_imgui_context,
                   });

  m_render_graph->execute(m_cmd_allocator);

  m_frame_arena.clear();
}

void SceneImpl::set_imgui_context(ImGuiContext *ctx) noexcept {
  m_imgui_context = ctx;
#if REN_IMGUI
  if (!ctx) {
    return;
  }
  ImGuiIO &io = ctx->IO;
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
  SampledTextureId texture =
      get_or_create_texture(image, {
                                       .mag_filter = Filter::Linear,
                                       .min_filter = Filter::Linear,
                                       .mipmap_filter = Filter::Linear,
                                       .wrap_u = WrappingMode::Repeat,
                                       .wrap_v = WrappingMode::Repeat,
                                   });
  io.Fonts->SetTexID((ImTextureID)(uintptr_t)texture);
#endif
}

void SceneImpl::enable_imgui(bool value) noexcept { m_imgui_enabled = value; }

} // namespace ren
