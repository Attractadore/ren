#include "Scene.hpp"
#include "Camera.inl"
#include "CommandAllocator.hpp"
#include "Descriptors.hpp"
#include "Device.hpp"
#include "Errors.hpp"
#include "Formats.inl"
#include "PipelineLoading.hpp"
#include "PostprocessPasses.hpp"
#include "RenderGraph.hpp"
#include "Support/Array.hpp"
#include "Support/Views.hpp"
#include "glsl/color_interface.hpp"
#include "glsl/encode.h"
#include "glsl/interface.hpp"

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

template <typename T, typename H> static auto get_id(Handle<H> handle) {
  return std::bit_cast<T>(std::bit_cast<unsigned>(handle) -
                          std::bit_cast<unsigned>(Handle<H>()));
}

template <typename H, typename T> static auto get_handle(T id) {
  return std::bit_cast<Handle<H>>(id + std::bit_cast<T>(Handle<H>()));
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
      }()) {}

Scene::Scene(Device &device, ResourceArena persistent_arena,
             Handle<DescriptorSetLayout> persistent_descriptor_set_layout,
             Handle<DescriptorPool> persistent_descriptor_pool,
             VkDescriptorSet persistent_descriptor_set,
             TextureIDAllocator tex_alloc)
    : m_device(&device), m_persistent_arena(std::move(persistent_arena)),
      m_frame_arena(device),
      m_persistent_descriptor_set_layout(persistent_descriptor_set_layout),
      m_persistent_descriptor_pool(persistent_descriptor_pool),
      m_persistent_descriptor_set(persistent_descriptor_set),
      m_texture_allocator(std::move(tex_alloc)), m_cmd_allocator(device) {
  m_pipeline_layout = create_color_pass_pipeline_layout(
      m_persistent_arena, m_persistent_descriptor_set_layout);
  m_pp_pipelines = load_postprocessing_pipelines(
      m_persistent_arena, m_persistent_descriptor_set_layout);
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
              REN_SET_DEBUG_NAME("Vertex buffer"),
              .heap = BufferHeap::Device,
              .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
              .size = vertex_buffer_size,
          })),
      .index_buffer =
          m_device->get_buffer_view(m_persistent_arena.create_buffer({
              REN_SET_DEBUG_NAME("Index buffer"),
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

  return get_id<RenMesh>(key);
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
      .width = desc.width,
      .height = desc.height,
      .mip_levels = get_mip_level_count(desc.width, desc.height),
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
    auto pipeline = [&] {
      auto pipeline = m_compiler.get_material_pipeline(desc);
      if (pipeline) {
        return *pipeline;
      }
      return m_compiler.compile_material_pipeline(
          m_persistent_arena, MaterialPipelineConfig{
                                  .material = desc,
                                  .layout = m_pipeline_layout,
                                  .rt_format = m_rt_format,
                                  .depth_format = m_depth_format,
                              });
    }();

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
    m_material_pipelines.push_back(pipeline);

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

  m_pp_opts.camera = {
      .aperture = desc.aperture,
      .shutter_time = desc.shutter_time,
      .iso = desc.iso,
  };

  m_pp_opts.exposure = {
      .compensation = desc.exposure_compensation,
      .mode = [&]() -> PostprocessingOptions::Exposure::Mode {
        switch (desc.exposure_mode) {
        case REN_EXPOSURE_MODE_CAMERA:
          return PostprocessingOptions::Exposure::Camera{};
        case REN_EXPOSURE_MODE_AUTOMATIC:
          return PostprocessingOptions::Exposure::Automatic{};
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
        .mesh = get_handle<Mesh>(desc.mesh),
        .material = desc.material,
        .matrix = glm::mat4(1.0f),
    });
    *out = get_id<RenMeshInst>(mesh_inst);
    ++out;
  }
}

void Scene::destroy_mesh_insts(
    std::span<const RenMeshInst> mesh_insts) noexcept {
  for (auto mesh_inst : mesh_insts) {
    m_mesh_insts.erase(get_handle<MeshInst>(mesh_inst));
  }
}

void Scene::set_mesh_inst_matrices(
    std::span<const RenMeshInst> mesh_insts,
    std::span<const RenMatrix4x4> matrices) noexcept {
  for (const auto &[mesh_inst, matrix] : zip(mesh_insts, matrices)) {
    m_mesh_insts[get_handle<MeshInst>(mesh_inst)].matrix =
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
    *out = get_id<RenDirLight>(light);
    ++out;
  }
};

void Scene::destroy_dir_lights(std::span<const RenDirLight> lights) noexcept {
  for (auto light : lights) {
    m_dir_lights.erase(get_handle<glsl::DirLight>(light));
  }
}

void Scene::config_dir_lights(std::span<const RenDirLight> lights,
                              std::span<const RenDirLightDesc> descs) {
  for (const auto &[light, desc] : zip(lights, descs)) {
    m_dir_lights[get_handle<glsl::DirLight>(light)] = {
        .color = glm::make_vec3(desc.color),
        .illuminance = desc.illuminance,
        .origin = glm::make_vec3(desc.origin),
    };
  }
}

struct UploadDataPassConfig {
  const DenseHandleMap<MeshInst> *mesh_insts;
  const DenseHandleMap<glsl::DirLight> *dir_lights;
  std::span<const glsl::Material> materials;
};

struct UploadDataPassResources {
  RGBufferID transform_matrix_buffer;
  RGBufferID normal_matrix_buffer;
  Optional<RGBufferID> dir_lights_buffer;
  RGBufferID materials_buffer;
};

struct UploadDataPassOutput {
  RGBufferID transform_matrix_buffer;
  RGBufferID normal_matrix_buffer;
  Optional<RGBufferID> dir_lights_buffer;
  RGBufferID materials_buffer;
};

static void run_upload_data_pass(Device &device, RenderGraph &rg,
                                 CommandBuffer &cmd,
                                 const UploadDataPassConfig &cfg,
                                 const UploadDataPassResources &rcs) {
  auto transform_matrix_buffer = rg.get_buffer(rcs.transform_matrix_buffer);
  auto *transform_matrices =
      device.map_buffer<glm::mat4x3>(transform_matrix_buffer);
  ranges::transform(cfg.mesh_insts->values(), transform_matrices,
                    [](const auto &mesh_inst) { return mesh_inst.matrix; });

  auto normal_matrix_buffer = rg.get_buffer(rcs.normal_matrix_buffer);
  auto *normal_matrices = device.map_buffer<glm::mat3>(normal_matrix_buffer);
  ranges::transform(
      cfg.mesh_insts->values(), normal_matrices, [](const auto &mesh_inst) {
        return glm::transpose(glm::inverse(glm::mat3(mesh_inst.matrix)));
      });

  rcs.dir_lights_buffer.map([&](RGBufferID buffer) {
    auto dir_lights_buffer = rg.get_buffer(buffer);
    auto *dir_lights = device.map_buffer<glsl::DirLight>(dir_lights_buffer);
    ranges::copy(cfg.dir_lights->values(), dir_lights);
  });

  auto materials_buffer = rg.get_buffer(rcs.materials_buffer);
  auto *materials = device.map_buffer<glsl::Material>(materials_buffer);
  ranges::copy(cfg.materials, materials);
}

static auto setup_upload_data_pass(Device &device, RenderGraph::Builder &rgb,
                                   const UploadDataPassConfig &cfg)
    -> UploadDataPassOutput {
  auto pass = rgb.create_pass("Upload data");

  UploadDataPassResources rcs = {};

  rcs.transform_matrix_buffer = pass.create_buffer(
      {
          REN_SET_DEBUG_NAME("Transform matrix buffer"),
          .heap = BufferHeap::Upload,
          .size = sizeof(glm::mat4x3) * cfg.mesh_insts->size(),
      },
      "Transform matrices", VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_NONE);

  rcs.normal_matrix_buffer = pass.create_buffer(
      {
          REN_SET_DEBUG_NAME("Normal matrix buffer"),
          .heap = BufferHeap::Upload,
          .size = sizeof(glm::mat3) * cfg.mesh_insts->size(),
      },
      "Normal matrices", VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_NONE);

  auto num_dir_lights = cfg.dir_lights->size();
  if (num_dir_lights > 0) {
    rcs.dir_lights_buffer = pass.create_buffer(
        {
            REN_SET_DEBUG_NAME("Dir lights buffer"),
            .heap = BufferHeap::Upload,
            .size = sizeof(glsl::DirLight) * num_dir_lights,
        },
        "Directional lights", VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_NONE);
  }

  rcs.materials_buffer = pass.create_buffer(
      {
          REN_SET_DEBUG_NAME("Materials buffer"),
          .heap = BufferHeap::Upload,
          .size = cfg.materials.size_bytes(),
      },
      "Materials", VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_NONE);

  pass.set_callback(
      [cfg, rcs](Device &device, RenderGraph &rg, CommandBuffer &cmd) {
        run_upload_data_pass(device, rg, cmd, cfg, rcs);
      });

  return {
      .transform_matrix_buffer = rcs.transform_matrix_buffer,
      .normal_matrix_buffer = rcs.normal_matrix_buffer,
      .dir_lights_buffer = rcs.dir_lights_buffer,
      .materials_buffer = rcs.materials_buffer,
  };
}

struct ColorPassConfig {
  VkFormat color_format;
  VkFormat depth_format;
  unsigned width;
  unsigned height;

  glm::mat4 proj;
  glm::mat4 view;
  glm::vec3 eye;

  unsigned num_dir_lights;

  const HandleMap<Mesh> *meshes;
  std::span<const Handle<GraphicsPipeline>> material_pipelines;
  const DenseHandleMap<MeshInst> *mesh_insts;

  ResourceArena *arena;

  VkDescriptorSet persistent_set;

  Handle<PipelineLayout> pipeline_layout;

  std::span<const RGBufferID> uploaded_vertex_buffers;
  std::span<const RGBufferID> uploaded_index_buffers;
  std::span<const RGTextureID> uploaded_textures;

  RGBufferID transform_matrix_buffer;
  RGBufferID normal_matrix_buffer;
  Optional<RGBufferID> dir_lights_buffer;
  RGBufferID materials_buffer;
};

struct ColorPassResources {
  RGTextureID rt;
  RGTextureID dst;
  RGBufferID uniform_buffer;
  VkDescriptorSet global_set;
};

struct ColorPassOutput {
  RGTextureID rt;
};

static void run_color_pass(Device &device, RenderGraph &rg, CommandBuffer &cmd,
                           const ColorPassConfig &cfg,
                           const ColorPassResources &rcs) {
  cmd.begin_rendering(rg.get_texture(rcs.rt), rg.get_texture(rcs.dst));
  cmd.set_viewport({.width = float(cfg.width),
                    .height = float(cfg.height),
                    .maxDepth = 1.0f});
  cmd.set_scissor_rect({.extent = {cfg.width, cfg.height}});

  auto transform_matrix_buffer = rg.get_buffer(cfg.transform_matrix_buffer);
  auto normal_matrix_buffer = rg.get_buffer(cfg.normal_matrix_buffer);
  auto dir_lights_buffer = cfg.dir_lights_buffer.map(
      [&](RGBufferID buffer) { return rg.get_buffer(buffer); });
  auto materials_buffer = rg.get_buffer(cfg.materials_buffer);

  auto uniform_buffer = rg.get_buffer(rcs.uniform_buffer);
  auto *uniforms = device.map_buffer<glsl::ColorUB>(uniform_buffer);
  *uniforms = {
      .transform_matrices_ptr =
          device.get_buffer_device_address(transform_matrix_buffer),
      .normal_matrices_ptr =
          device.get_buffer_device_address(normal_matrix_buffer),
      .materials_ptr = device.get_buffer_device_address(materials_buffer),
      .directional_lights_ptr = dir_lights_buffer.map_or(
          [&](const BufferView &view) {
            return device.get_buffer_device_address(view);
          },
          u64(0)),
      .proj_view = cfg.proj * cfg.view,
      .eye = cfg.eye,
      .num_dir_lights = cfg.num_dir_lights,
  };

  std::array descriptor_sets = {cfg.persistent_set};
  cmd.bind_descriptor_sets(VK_PIPELINE_BIND_POINT_GRAPHICS, cfg.pipeline_layout,
                           0, descriptor_sets);

  auto ub_ptr = device.get_buffer_device_address(uniform_buffer);
  for (const auto &&[i, mesh_inst] : enumerate(cfg.mesh_insts->values())) {
    const auto &mesh = (*cfg.meshes)[mesh_inst.mesh];
    auto material = mesh_inst.material;

    cmd.bind_graphics_pipeline(cfg.material_pipelines[material]);

    auto address = device.get_buffer_device_address(mesh.vertex_buffer);
    auto positions_offset = mesh.attribute_offsets[MESH_ATTRIBUTE_POSITIONS];
    auto normals_offset = mesh.attribute_offsets[MESH_ATTRIBUTE_NORMALS];
    auto colors_offset = mesh.attribute_offsets[MESH_ATTRIBUTE_COLORS];
    auto uvs_offset = mesh.attribute_offsets[MESH_ATTRIBUTE_UVS];
    glsl::ColorConstants pcs = {
        .ub_ptr = ub_ptr,
        .positions_ptr = address + positions_offset,
        .colors_ptr =
            (colors_offset != ATTRIBUTE_UNUSED) ? address + colors_offset : 0,
        .normals_ptr = address + normals_offset,
        .uvs_ptr = (uvs_offset != ATTRIBUTE_UNUSED) ? address + uvs_offset : 0,
        .matrix_index = unsigned(i),
        .material_index = material,
    };
    cmd.set_push_constants(
        cfg.pipeline_layout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, pcs);

    cmd.bind_index_buffer(mesh.index_buffer, mesh.index_format);
    cmd.draw_indexed({
        .num_indices = mesh.num_indices,
    });
  }

  cmd.end_rendering();
}

static auto setup_color_pass(Device &device, RenderGraph::Builder &rgb,
                             const ColorPassConfig &cfg) -> ColorPassOutput {
  auto pass = rgb.create_pass("Color");

  ColorPassResources rcs = {};

  for (auto buffer : cfg.uploaded_vertex_buffers) {
    pass.read_buffer(buffer, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                     VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT);
  }

  for (auto buffer : cfg.uploaded_index_buffers) {
    pass.read_buffer(buffer, VK_ACCESS_2_INDEX_READ_BIT,
                     VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT);
  }

  for (auto texture : cfg.uploaded_textures) {
    pass.read_texture(texture, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }

  pass.read_buffer(cfg.transform_matrix_buffer,
                   VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                   VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT);

  pass.read_buffer(cfg.normal_matrix_buffer,
                   VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                   VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT);

  cfg.dir_lights_buffer.map([&](RGBufferID buffer) {
    pass.read_buffer(buffer, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
  });

  pass.read_buffer(cfg.materials_buffer, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);

  rcs.uniform_buffer = pass.create_buffer(
      {
          REN_SET_DEBUG_NAME("Color pass uniform buffer"),
          .heap = BufferHeap::Upload,
          .size = sizeof(glsl::ColorUB),
      },
      "Global data UBO", VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
      VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);

  rcs.rt = pass.create_texture(
      {
          REN_SET_DEBUG_NAME("Color buffer"),
          .format = cfg.color_format,
          .width = cfg.width,
          .height = cfg.height,
      },
      "Color buffer", VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

  rcs.dst = pass.create_texture(
      {
          REN_SET_DEBUG_NAME("Depth buffer"),
          .format = cfg.depth_format,
          .width = cfg.width,
          .height = cfg.height,
      },
      "Depth buffer",
      VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
          VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
      VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
          VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
      VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

  pass.set_callback(
      [cfg, rcs](Device &device, RenderGraph &rg, CommandBuffer &cmd) {
        run_color_pass(device, rg, cmd, cfg, rcs);
      });

  return {
      .rt = rcs.rt,
  };
}

void Scene::draw(Swapchain &swapchain) {
  RenderGraph::Builder rgb;

  auto uploaded_vertex_buffers =
      m_staged_vertex_buffers | map([&](const BufferView &buffer) {
        return rgb.import_buffer(buffer, "Uploaded mesh vertex buffer",
                                 VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                 VK_PIPELINE_STAGE_2_COPY_BIT);
      }) |
      ranges::to<Vector>();
  m_staged_vertex_buffers.clear();

  auto uploaded_index_buffers =
      m_staged_index_buffers | map([&](const BufferView &buffer) {
        return rgb.import_buffer(buffer, "Uploaded mesh index buffer",
                                 VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                 VK_PIPELINE_STAGE_2_COPY_BIT);
      }) |
      ranges::to<Vector>();
  m_staged_index_buffers.clear();

  auto uploaded_textures =
      m_staged_textures | map([&](Handle<Texture> texture) {
        return rgb.import_texture(
            m_device->get_texture_view(texture), "Uploaded material texture",
            VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_BLIT_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
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

  if (!m_mesh_insts.empty()) {
    auto frame_resources =
        setup_upload_data_pass(*m_device, rgb,
                               UploadDataPassConfig{
                                   .mesh_insts = &m_mesh_insts,
                                   .dir_lights = &m_dir_lights,
                                   .materials = m_materials,
                               });

    // Draw scene
    auto [rt] = setup_color_pass(
        *m_device, rgb,
        ColorPassConfig{
            .color_format = m_rt_format,
            .depth_format = m_depth_format,
            .width = m_viewport_width,
            .height = m_viewport_height,
            .proj = get_projection_matrix(
                m_camera, float(m_viewport_width) / float(m_viewport_height)),
            .view = get_view_matrix(m_camera),
            .eye = m_camera.position,
            .num_dir_lights = unsigned(m_dir_lights.size()),
            .meshes = &m_meshes,
            .material_pipelines = m_material_pipelines,
            .mesh_insts = &m_mesh_insts,
            .arena = &m_persistent_arena,
            .persistent_set = m_persistent_descriptor_set,
            .pipeline_layout = m_pipeline_layout,
            .uploaded_vertex_buffers = uploaded_vertex_buffers,
            .uploaded_index_buffers = uploaded_index_buffers,
            .uploaded_textures = uploaded_textures,
            .transform_matrix_buffer = frame_resources.transform_matrix_buffer,
            .normal_matrix_buffer = frame_resources.normal_matrix_buffer,
            .dir_lights_buffer = frame_resources.dir_lights_buffer,
            .materials_buffer = frame_resources.materials_buffer,
        });

    auto [pprt] =
        setup_postprocess_passes(*m_device, rgb,
                                 PostprocessPassesConfig{
                                     .texture = rt,
                                     .options = m_pp_opts,
                                     .texture_allocator = &m_texture_allocator,
                                     .pipelines = m_pp_pipelines,
                                 });

    rgb.present(swapchain, pprt,
                m_frame_arena.create_semaphore(
                    {REN_SET_DEBUG_NAME("Acquire semaphore")}),
                m_frame_arena.create_semaphore(
                    {REN_SET_DEBUG_NAME("Present semaphore")}));

    auto rg = rgb.build(*m_device, m_frame_arena);

    rg.execute(*m_device, m_cmd_allocator);
  }

  next_frame();
}

} // namespace ren
