#include "Scene.hpp"
#include "Camera.inl"
#include "CommandBuffer.hpp"
#include "Errors.hpp"
#include "Formats.inl"
#include "Passes/AutomaticExposure.hpp"
#include "Passes/Color.hpp"
#include "Passes/Exposure.hpp"
#include "Passes/PostProcessing.hpp"
#include "Passes/Upload.hpp"
#include "glsl/encode.hpp"

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

Scene::Scene(Device &device)
    : Scene([&device] {
        ResourceArena persistent_arena(device);

        auto persistent_descriptor_set_layout =
            create_persistent_descriptor_set_layout(persistent_arena);
        auto [persistent_descriptor_pool, persistent_descriptor_set] =
            allocate_descriptor_pool_and_set(device, persistent_arena,
                                             persistent_descriptor_set_layout);

        return Scene(device, std::move(persistent_arena),
                     persistent_descriptor_set_layout,
                     persistent_descriptor_pool, persistent_descriptor_set,
                     TextureIDAllocator(device, persistent_descriptor_set,
                                        persistent_descriptor_set_layout));
      }()) {

#if !__clang__
  static_assert(std::bit_cast<u32>(SlotMapKey()) == 0,
                "C handles can't be directly converted to SlotMap keys");
#else
  assert(std::bit_cast<u32>(SlotMapKey()) == 0);
#endif
}

Scene::Scene(Device &device, ResourceArena persistent_arena,
             Handle<DescriptorSetLayout> persistent_descriptor_set_layout,
             Handle<DescriptorPool> persistent_descriptor_pool,
             VkDescriptorSet persistent_descriptor_set,
             TextureIDAllocator tex_alloc)
    : m_device(&device), m_persistent_arena(std::move(persistent_arena)),
      m_frame_arena(device), m_render_graph(device),
      m_persistent_descriptor_set_layout(persistent_descriptor_set_layout),
      m_persistent_descriptor_pool(persistent_descriptor_pool),
      m_persistent_descriptor_set(persistent_descriptor_set),
      m_texture_allocator(std::move(tex_alloc)), m_cmd_allocator(device) {
  m_pipelines =
      load_pipelines(m_persistent_arena, m_persistent_descriptor_set_layout);
}

void Scene::next_frame() {
  m_frame_arena.clear();
  m_device->next_frame();
  m_cmd_allocator.next_frame();
  m_texture_allocator.next_frame();
}

RenMesh Scene::create_mesh(const RenMeshDesc &desc) {
  std::array<std::span<const std::byte>, MESH_ATTRIBUTE_COUNT>
      upload_attributes;

  upload_attributes[MESH_ATTRIBUTE_POSITIONS] =
      std::as_bytes(std::span(desc.positions, desc.num_vertices));

  if (desc.colors) {
    upload_attributes[MESH_ATTRIBUTE_COLORS] =
        std::as_bytes(std::span(desc.colors, desc.num_vertices));
  }

  if (!desc.normals) {
    todo("Normals generation not implemented!");
  }
  upload_attributes[MESH_ATTRIBUTE_NORMALS] =
      std::as_bytes(std::span(desc.normals, desc.num_vertices));

  if (desc.tangents) {
    todo("Normal mapping not implemented!");
  }

  if (desc.uvs) {
    upload_attributes[MESH_ATTRIBUTE_UVS] =
        std::as_bytes(std::span(desc.uvs, desc.num_vertices));
  }

  if (!desc.indices) {
    todo("Index buffer generation not implemented!");
  }

  auto used_attributes = range(int(MESH_ATTRIBUTE_COUNT)) |
                         filter_map([&](int i) -> Optional<MeshAttribute> {
                           auto mesh_attribute = static_cast<MeshAttribute>(i);
                           if (not upload_attributes[mesh_attribute].empty()) {
                             return mesh_attribute;
                           }
                           return None;
                         });

  auto get_mesh_attribute_size = [](MeshAttribute attribute) -> unsigned {
    switch (attribute) {
    default:
      unreachable("Unknown mesh attribute {}", int(attribute));
    case MESH_ATTRIBUTE_POSITIONS:
      return sizeof(glm::vec3);
    case MESH_ATTRIBUTE_NORMALS:
      return sizeof(glsl::normal_t);
    case MESH_ATTRIBUTE_COLORS:
      return sizeof(glsl::color_t);
    case MESH_ATTRIBUTE_UVS:
      return sizeof(glm::vec2);
    }
  };

  auto vertex_buffer_size =
      desc.num_vertices *
      ranges::accumulate(used_attributes | map(get_mesh_attribute_size), 0);
  auto index_buffer_size = desc.num_indices * sizeof(unsigned);

  Mesh mesh = {
      .vertex_buffer =
          m_device->get_buffer_view(m_persistent_arena.create_buffer({
              .name = "Vertex buffer",
              .heap = BufferHeap::Device,
              .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
              .size = vertex_buffer_size,
          })),
      .index_buffer =
          m_device->get_buffer_view(m_persistent_arena.create_buffer({
              .name = "Index buffer",
              .heap = BufferHeap::Device,
              .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                       VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
              .size = index_buffer_size,
          })),
      .num_vertices = desc.num_vertices,
      .num_indices = desc.num_indices,
      .index_format = VK_INDEX_TYPE_UINT32,
  };

  mesh.attribute_offsets.fill(ATTRIBUTE_UNUSED);

  size_t offset = 0;
  for (auto mesh_attribute : used_attributes) {
    mesh.attribute_offsets[mesh_attribute] = offset;
    auto data = upload_attributes[mesh_attribute];
    auto dst = mesh.vertex_buffer.subbuffer(offset);
    switch (mesh_attribute) {
    default:
      assert(!"Unknown mesh attribute");
    case MESH_ATTRIBUTE_POSITIONS: {
      auto positions = reinterpret_span<const glm::vec3>(data);
      m_resource_uploader.stage_buffer(*m_device, m_frame_arena, positions,
                                       dst);
      offset += size_bytes(positions);
    } break;
    case MESH_ATTRIBUTE_NORMALS: {
      auto normals =
          reinterpret_span<const glm::vec3>(data) | map(glsl::encode_normal);
      m_resource_uploader.stage_buffer(*m_device, m_frame_arena, normals, dst);
      offset += size_bytes(normals);
    } break;
    case MESH_ATTRIBUTE_COLORS: {
      auto colors =
          reinterpret_span<const glm::vec4>(data) | map(glsl::encode_color);
      m_resource_uploader.stage_buffer(*m_device, m_frame_arena, colors, dst);
      offset += size_bytes(colors);
    } break;
    case MESH_ATTRIBUTE_UVS: {
      auto uvs = reinterpret_span<const glm::vec2>(data);
      m_resource_uploader.stage_buffer(*m_device, m_frame_arena, uvs, dst);
      offset += size_bytes(uvs);
    } break;
    }
  }

  auto indices = std::span(desc.indices, desc.num_indices);
  m_resource_uploader.stage_buffer(*m_device, m_frame_arena, indices,
                                   mesh.index_buffer);

  if (!m_device->map_buffer(mesh.vertex_buffer)) {
    m_staged_vertex_buffers.push_back(mesh.vertex_buffer);
  }

  if (!m_device->map_buffer(mesh.index_buffer)) {
    m_staged_index_buffers.push_back(mesh.index_buffer);
  }

  auto key = m_meshes.emplace(std::move(mesh));

  return std::bit_cast<RenMesh>(key);
}

auto Scene::get_or_create_sampler(const RenSampler &sampler)
    -> Handle<Sampler> {
  auto &handle = m_samplers[sampler];
  if (!handle) {
    handle = m_persistent_arena.create_sampler({
        .mag_filter = getVkFilter(sampler.mag_filter),
        .min_filter = getVkFilter(sampler.min_filter),
        .mipmap_mode = getVkSamplerMipmapMode(sampler.mipmap_filter),
        .address_mode_u = getVkSamplerAddressMode(sampler.wrap_u),
        .address_mode_v = getVkSamplerAddressMode(sampler.wrap_v),
    });
  }
  return handle;
}

auto Scene::get_or_create_texture(const RenTexture &texture)
    -> SampledTextureID {
  auto view = m_device->get_texture_view(m_images[texture.image]);
  view.swizzle = getTextureSwizzle(texture.swizzle);
  auto sampler = get_or_create_sampler(texture.sampler);
  return m_texture_allocator.allocate_sampled_texture(view, sampler);
}

auto Scene::create_image(const RenImageDesc &desc) -> RenImage {
  auto image = static_cast<RenImage>(m_images.size());
  auto format = getVkFormat(desc.format);
  auto texture = m_persistent_arena.create_texture({
      .type = VK_IMAGE_TYPE_2D,
      .format = format,
      .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
               VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
      .size = {desc.width, desc.height, 1},
      .num_mip_levels = get_mip_level_count(desc.width, desc.height),
  });
  m_images.push_back(texture);
  auto image_size = desc.width * desc.height * get_format_size(format);
  m_resource_uploader.stage_texture(
      *m_device, m_frame_arena,
      std::span(reinterpret_cast<const std::byte *>(desc.data), image_size),
      texture);
  m_staged_textures.push_back(texture);
  return image;
}

void Scene::create_materials(std::span<const RenMaterialDesc> descs,
                             RenMaterial *out) {
  for (const auto &desc : descs) {
    glsl::Material material = {
        .base_color = glm::make_vec4(desc.base_color_factor),
        .base_color_texture = [&]() -> unsigned {
          if (desc.color_tex.image) {
            return get_or_create_texture(desc.color_tex);
          }
          return 0;
        }(),
        .metallic = desc.metallic_factor,
        .roughness = desc.roughness_factor,
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
    auto mesh_inst = m_mesh_insts.insert({
        .mesh = std::bit_cast<Handle<Mesh>>(desc.mesh),
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
    m_mesh_insts.erase(std::bit_cast<Handle<MeshInst>>(mesh_inst));
  }
}

void Scene::set_mesh_inst_matrices(
    std::span<const RenMeshInst> mesh_insts,
    std::span<const RenMatrix4x4> matrices) noexcept {
  for (const auto &[mesh_inst, matrix] : zip(mesh_insts, matrices)) {
    m_mesh_insts[std::bit_cast<Handle<MeshInst>>(mesh_inst)].matrix =
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

void Scene::draw(Swapchain &swapchain) {
  RGBuilder rgb(m_render_graph);

  auto uploaded_vertex_buffers =
      m_staged_vertex_buffers | map([&](const BufferView &buffer) {
        return rgb.import_buffer({
            .name = "Uploaded mesh vertices",
            .buffer = buffer,
            .state =
                {
                    .stages = VK_PIPELINE_STAGE_2_COPY_BIT,
                    .accesses = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                },
        });
      }) |
      ranges::to<Vector>();
  m_staged_vertex_buffers.clear();

  auto uploaded_index_buffers =
      m_staged_index_buffers | map([&](const BufferView &buffer) {
        return rgb.import_buffer({
            .name = "Uploaded mesh indices",
            .buffer = buffer,
            .state =
                {
                    .stages = VK_PIPELINE_STAGE_2_COPY_BIT,
                    .accesses = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                },
        });
      }) |
      ranges::to<Vector>();
  m_staged_index_buffers.clear();

  auto uploaded_textures =
      m_staged_textures | map([&](Handle<Texture> texture) {
        return rgb.import_texture({
            .name = "Uploaded material texture",
            .texture = m_device->get_texture_view(texture),
            .state =
                {
                    .stages = VK_PIPELINE_STAGE_2_BLIT_BIT,
                    .accesses = VK_ACCESS_2_TRANSFER_READ_BIT,
                    .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                },
        });
      }) |
      ranges::to<Vector>();
  m_staged_textures.clear();

  m_resource_uploader.record_upload(*m_device, m_cmd_allocator)
      .map([&](CommandBuffer cmd) {
        m_device->graphicsQueueSubmit(asSpan(VkCommandBufferSubmitInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = cmd.get(),
        }));
      });

  RGTextureID texture;
  std::tie(texture, m_temporal_resources) = setup_all_passes(
      *m_device, rgb,
      {
          .temporal_resources = &m_temporal_resources,
          .pipelines = &m_pipelines,
          .texture_allocator = &m_texture_allocator,
          .uploaded_vertex_buffers = uploaded_vertex_buffers,
          .uploaded_index_buffers = uploaded_index_buffers,
          .uploaded_textures = uploaded_textures,
          .viewport_size = {m_viewport_width, m_viewport_height},
          .camera = &m_camera,
          .meshes = &m_meshes,
          .mesh_insts = m_mesh_insts.values(),
          .directional_lights = m_dir_lights.values(),
          .materials = m_materials,
          .pp_opts = &m_pp_opts,
      });

  rgb.present(swapchain, texture,
              m_frame_arena.create_semaphore({
                  .name = "Acquire semaphore",
              }),
              m_frame_arena.create_semaphore({
                  .name = "Present semaphore",
              }));

  rgb.build(*m_device);

  m_render_graph.execute(*m_device, m_cmd_allocator);

  next_frame();
}

} // namespace ren
