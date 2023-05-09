#include "Scene.hpp"
#include "Camera.inl"
#include "CommandAllocator.hpp"
#include "Descriptors.hpp"
#include "Device.hpp"
#include "Errors.hpp"
#include "Formats.inl"
#include "RenderGraph.hpp"
#include "ResourceUploader.inl"
#include "Support/Array.hpp"
#include "Support/Views.hpp"
#include "hlsl/encode.h"

#include <glm/gtc/type_ptr.hpp>
#include <range/v3/algorithm.hpp>
#include <range/v3/numeric.hpp>
#include <range/v3/range.hpp>

namespace ren {

static MeshMap::key_type get_mesh_key(RenMesh mesh) {
  return std::bit_cast<MeshMap::key_type>(mesh - 1);
}

static RenMesh get_mesh_id(MeshMap::key_type mesh_key) {
  return std::bit_cast<RenMesh>(std::bit_cast<RenMesh>(mesh_key) + 1);
}

static MeshInstanceMap::key_type get_mesh_inst_key(RenMeshInst mesh_inst) {
  return std::bit_cast<MeshInstanceMap::key_type>(mesh_inst - 1);
}

static RenMeshInst get_mesh_inst_id(MeshInstanceMap::key_type mesh_inst_key) {
  return std::bit_cast<RenMeshInst>(std::bit_cast<RenMeshInst>(mesh_inst_key) +
                                    1);
}

static DirLightMap::key_type get_dir_light_key(RenDirLight dir_light) {
  return std::bit_cast<DirLightMap::key_type>(dir_light - 1);
}

static RenDirLight get_dir_light_id(DirLightMap::key_type dir_light_key) {
  return std::bit_cast<RenDirLight>(std::bit_cast<RenDirLight>(dir_light_key) +
                                    1);
}

static auto get_mesh(const MeshMap &m_meshes, RenMesh mesh) -> const Mesh & {
  auto key = get_mesh_key(mesh);
  assert(m_meshes.contains(key) && "Unknown mesh");
  return m_meshes[key];
}

static auto get_mesh(MeshMap &m_meshes, RenMesh mesh) -> Mesh & {
  auto key = get_mesh_key(mesh);
  assert(m_meshes.contains(key) && "Unknown mesh");
  return m_meshes[key];
}

static auto get_mesh_inst(const MeshInstanceMap &mesh_insts,
                          RenMeshInst mesh_inst) -> const MeshInst & {
  auto key = get_mesh_inst_key(mesh_inst);
  assert(mesh_insts.contains(key) && "Unknown mesh instance");
  return mesh_insts[key];
}

static auto get_mesh_inst(MeshInstanceMap &mesh_insts, RenMeshInst mesh_inst)
    -> MeshInst & {
  auto key = get_mesh_inst_key(mesh_inst);
  assert(mesh_insts.contains(key) && "Unknown mesh instance");
  return mesh_insts[key];
}

auto get_dir_light(const DirLightMap &m_dir_lights, RenDirLight light)
    -> const hlsl::DirLight & {
  auto key = get_dir_light_key(light);
  assert(m_dir_lights.contains(key) && "Unknown light");
  return m_dir_lights[key];
}

auto get_dir_light(DirLightMap &m_dir_lights, RenDirLight light)
    -> hlsl::DirLight & {
  auto key = get_dir_light_key(light);
  assert(m_dir_lights.contains(key) && "Unknown light");
  return m_dir_lights[key];
}

namespace {

auto merge_set_layout_descs(const DescriptorSetLayoutDesc &lhs,
                            const DescriptorSetLayoutDesc &rhs)
    -> DescriptorSetLayoutDesc {
  DescriptorSetLayoutDesc result = {};
  result.flags = lhs.flags | rhs.flags;
  for (auto &&[result, lhs, rhs] :
       zip(result.bindings, lhs.bindings, rhs.bindings)) {
    if (lhs.stages) {
      result = lhs;
    } else {
      result = rhs;
    }
    result.stages |= lhs.stages | rhs.stages;
  }
  return result;
}

auto reflect_descriptor_set_layouts(const AssetLoader &loader)
    -> StaticVector<DescriptorSetLayoutDesc, MAX_DESCIPTOR_SETS> {
  Vector<std::byte> buffer;
  std::array shaders = {
      "ReflectionVertexShader.spv",
      "ReflectionFragmentShader.spv",
  };

  StaticVector<DescriptorSetLayoutDesc, MAX_DESCIPTOR_SETS> set_layout_descs;
  for (auto shader : shaders) {
    loader.load_file(shader, buffer);
    auto shader_set_layout_descs = get_set_layout_descs(buffer);
    for (auto &&[layout, shader_layout] :
         zip(set_layout_descs, shader_set_layout_descs)) {
      layout = merge_set_layout_descs(layout, shader_layout);
    }
    set_layout_descs.append(shader_set_layout_descs |
                            ranges::views::drop(set_layout_descs.size()));
  }

  auto &persistent_set = set_layout_descs[hlsl::PERSISTENT_SET];
  persistent_set.flags |=
      VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
  for (auto slot : {hlsl::SAMPLERS_SLOT, hlsl::TEXTURES_SLOT}) {
    persistent_set.bindings[slot].flags |=
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
  }

  return set_layout_descs;
}

auto reflect_material_pipeline_layout(Device &device, const AssetLoader &loader)
    -> PipelineLayout {
  auto set_layout_descs = reflect_descriptor_set_layouts(loader);

  PipelineLayoutDesc desc = {};
  for (const DescriptorSetLayoutDesc &set_layout_desc : set_layout_descs) {
    desc.set_layouts.push_back(
        device.create_descriptor_set_layout(set_layout_desc));
  }
  desc.push_constants = {
      .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
      .size = sizeof(hlsl::PushConstants),
  };

  return device.create_pipeline_layout(std::move(desc));
}

} // namespace

Scene::Scene(Device &device) : m_device(&device), m_cmd_allocator(*m_device) {
  m_asset_loader.add_search_directory(c_assets_directory);

  m_pipeline_layout =
      reflect_material_pipeline_layout(*m_device, m_asset_loader);

  std::tie(m_persistent_descriptor_pool, m_persistent_descriptor_set) =
      m_device->allocate_descriptor_set(
          m_pipeline_layout.desc->set_layouts[hlsl::PERSISTENT_SET]);
}

Scene::~Scene() {
  m_frame_arena.clear(*m_device);
  m_persistent_arena.clear(*m_device);
}

void Scene::next_frame() {
  m_frame_arena.clear(*m_device);
  m_device->next_frame();
  m_cmd_allocator.next_frame();
  m_descriptor_set_allocator.next_frame(*m_device);
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
      return sizeof(hlsl::normal_t);
    case MESH_ATTRIBUTE_COLORS:
      return sizeof(hlsl::color_t);
    case MESH_ATTRIBUTE_UVS:
      return sizeof(glm::vec2);
    }
  };

  auto vertex_buffer_size =
      desc.num_vertices *
      ranges::accumulate(used_attributes | map(get_mesh_attribute_size), 0);
  auto index_buffer_size = desc.num_indices * sizeof(unsigned);

  Mesh mesh = {
      .vertex_buffer = m_persistent_arena.create_buffer(
          {
              REN_SET_DEBUG_NAME("Vertex buffer"),
              .heap = BufferHeap::Device,
              .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
              .size = vertex_buffer_size,
          },
          *m_device),
      .index_buffer = m_persistent_arena.create_buffer(
          {
              REN_SET_DEBUG_NAME("Index buffer"),
              .heap = BufferHeap::Device,
              .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                       VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
              .size = index_buffer_size,
          },
          *m_device),
      .num_vertices = desc.num_vertices,
      .num_indices = desc.num_indices,
      .index_format = VK_INDEX_TYPE_UINT32,
  };

  mesh.attribute_offsets.fill(ATTRIBUTE_UNUSED);

  size_t offset = 0;
  for (auto mesh_attribute : used_attributes) {
    mesh.attribute_offsets[mesh_attribute] = offset;
    auto data = upload_attributes[mesh_attribute];
    switch (mesh_attribute) {
    default:
      assert(!"Unknown mesh attribute");
    case MESH_ATTRIBUTE_POSITIONS: {
      auto positions = reinterpret_span<const glm::vec3>(data);
      m_resource_uploader.stage_buffer(*m_device, m_frame_arena, positions,
                                       mesh.vertex_buffer, offset);
      offset += size_bytes(positions);
    } break;
    case MESH_ATTRIBUTE_NORMALS: {
      auto normals =
          reinterpret_span<const glm::vec3>(data) | map(hlsl::encode_normal);
      m_resource_uploader.stage_buffer(*m_device, m_frame_arena, normals,
                                       mesh.vertex_buffer, offset);
      offset += size_bytes(normals);
    } break;
    case MESH_ATTRIBUTE_COLORS: {
      auto colors =
          reinterpret_span<const glm::vec4>(data) | map(hlsl::encode_color);
      m_resource_uploader.stage_buffer(*m_device, m_frame_arena, colors,
                                       mesh.vertex_buffer, offset);
      offset += size_bytes(colors);
    } break;
    case MESH_ATTRIBUTE_UVS: {
      auto uvs = reinterpret_span<const glm::vec2>(data);
      m_resource_uploader.stage_buffer(*m_device, m_frame_arena, uvs,
                                       mesh.vertex_buffer, offset);
      offset += size_bytes(uvs);
    } break;
    }
  }

  auto indices = std::span(desc.indices, desc.num_indices);
  m_resource_uploader.stage_buffer(*m_device, m_frame_arena, indices,
                                   mesh.index_buffer);

  if (!m_device->get_buffer(mesh.vertex_buffer).ptr) {
    m_staged_vertex_buffers.push_back(mesh.vertex_buffer);
  }

  if (!m_device->get_buffer(mesh.index_buffer).ptr) {
    m_staged_index_buffers.push_back(mesh.index_buffer);
  }

  auto key = m_meshes.emplace(std::move(mesh));

  return get_mesh_id(key);
}

auto Scene::get_or_create_sampler(const RenTexture &texture) -> SamplerID {
  SamplerDesc desc = {
      .mag_filter = getVkFilter(texture.sampler.mag_filter),
      .min_filter = getVkFilter(texture.sampler.min_filter),
      .mipmap_mode = getVkSamplerMipmapMode(texture.sampler.mipmap_filter),
      .address_Mode_u = getVkSamplerAddressMode(texture.sampler.wrap_u),
      .address_Mode_v = getVkSamplerAddressMode(texture.sampler.wrap_v),
  };

  unsigned index = ranges::distance(m_sampler_descs.begin(),
                                    ranges::find(m_sampler_descs, desc));
  assert(index < hlsl::NUM_SAMPLERS);

  if (index == m_sampler_descs.size()) {
    m_sampler_descs.push_back(desc);
    m_samplers.push_back(m_device->create_sampler(desc));

    DescriptorSetWriter(
        *m_device, m_persistent_descriptor_set,
        m_pipeline_layout.desc->set_layouts[hlsl::PERSISTENT_SET])
        .add_sampler(hlsl::SAMPLERS_SLOT, m_samplers.back(), index)
        .write();
  }

  return SamplerID{index};
}

auto Scene::get_or_create_texture(const RenTexture &texture) -> TextureID {
  unsigned index = m_num_textures++;
  assert(index < hlsl::NUM_TEXTURES);

  auto view =
      TextureHandleView::from_texture(*m_device, m_images[texture.image]);
  view.swizzle = getVkComponentMapping(texture.swizzle);

  DescriptorSetWriter(*m_device, m_persistent_descriptor_set,
                      m_pipeline_layout.desc->set_layouts[hlsl::PERSISTENT_SET])
      .add_texture(hlsl::TEXTURES_SLOT, view, index)
      .write();

  return TextureID{index};
}

auto Scene::create_image(const RenImageDesc &desc) -> RenImage {
  auto image = static_cast<RenImage>(m_images.size());
  auto format = getVkFormat(desc.format);
  auto texture = m_persistent_arena.create_texture(
      {
          .type = VK_IMAGE_TYPE_2D,
          .format = format,
          .usage = VK_IMAGE_USAGE_SAMPLED_BIT |
                   VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
          .width = desc.width,
          .height = desc.height,
          .mip_levels = get_mip_level_count(desc.width, desc.height),
      },
      *m_device);
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
          *m_device, m_asset_loader,
          MaterialPipelineConfig{
              .material = desc,
              .layout = m_pipeline_layout,
              .rt_format = m_rt_format,
              .depth_format = m_depth_format,
          });
    }();

    hlsl::Material material = {
        .base_color = glm::make_vec4(desc.base_color_factor),
        .metallic = desc.metallic_factor,
        .roughness = desc.roughness_factor,
    };

    const auto &base_color_texture = desc.color_tex;
    if (base_color_texture.image) {
      material.base_color_texture = get_or_create_texture(base_color_texture);
      material.base_color_sampler = get_or_create_sampler(base_color_texture);
    }

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
        unreachable("Unknown projection {}", int(desc.projection));
      }(),
  };
  m_viewport_width = desc.width;
  m_viewport_height = desc.height;
}

void Scene::create_mesh_insts(std::span<const RenMeshInstDesc> descs,
                              RenMeshInst *out) {
  for (const auto &desc : descs) {
    *(out++) = get_mesh_inst_id(m_mesh_insts.insert({
        .mesh = desc.mesh,
        .material = desc.material,
        .matrix = glm::mat4(1.0f),
    }));
  }
}

void Scene::destroy_mesh_insts(
    std::span<const RenMeshInst> mesh_insts) noexcept {
  for (auto mesh_inst : mesh_insts)
    m_mesh_insts.erase(get_mesh_inst_key(mesh_inst));
}

void Scene::set_mesh_inst_matrices(
    std::span<const RenMeshInst> mesh_insts,
    std::span<const RenMatrix4x4> matrices) noexcept {
  for (const auto &[mesh_inst, matrix] : zip(mesh_insts, matrices))
    get_mesh_inst(m_mesh_insts, mesh_inst).matrix = glm::make_mat4(matrix[0]);
}

void Scene::create_dir_lights(std::span<const RenDirLightDesc> descs,
                              RenDirLight *out) {
  for (const auto &desc : descs) {
    auto key = m_dir_lights.insert(hlsl::DirLight{
        .color = glm::make_vec3(desc.color),
        .illuminance = desc.illuminance,
        .origin = glm::make_vec3(desc.origin),
    });
    *out = get_dir_light_id(key);
    ++out;
  }
};

void Scene::destroy_dir_lights(std::span<const RenDirLight> lights) noexcept {
  for (auto light : lights) {
    auto key = get_dir_light_key(light);
    assert(m_dir_lights.contains(key) && "Unknown light");
    m_dir_lights.erase(key);
  }
}

void Scene::config_dir_lights(std::span<const RenDirLight> lights,
                              std::span<const RenDirLightDesc> descs) {
  for (const auto &[light, desc] : zip(lights, descs)) {
    get_dir_light(m_dir_lights, light) = {
        .color = glm::make_vec3(desc.color),
        .illuminance = desc.illuminance,
        .origin = glm::make_vec3(desc.origin),
    };
  }
}

struct UploadDataPassConfig {
  const DenseSlotMap<MeshInst> *mesh_insts;
  const DenseSlotMap<hlsl::DirLight> *dir_lights;
  std::span<const hlsl::Material> materials;
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

static void run_upload_data_pass(Device &device, CommandBuffer &cmd,
                                 RenderGraph &rg,
                                 const UploadDataPassConfig &cfg,
                                 const UploadDataPassResources &rcs) {
  auto transform_matrix_buffer = rg.get_buffer(rcs.transform_matrix_buffer);
  auto *transform_matrices =
      transform_matrix_buffer.map<hlsl::model_matrix_t>();
  ranges::transform(cfg.mesh_insts->values(), transform_matrices,
                    [](const auto &mesh_inst) { return mesh_inst.matrix; });

  auto normal_matrix_buffer = rg.get_buffer(rcs.normal_matrix_buffer);
  auto *normal_matrices = normal_matrix_buffer.map<hlsl::normal_matrix_t>();
  ranges::transform(
      cfg.mesh_insts->values(), normal_matrices, [](const auto &mesh_inst) {
        return glm::transpose(glm::inverse(glm::mat3(mesh_inst.matrix)));
      });

  rcs.dir_lights_buffer.map([&](RGBufferID buffer) {
    auto dir_lights_buffer = rg.get_buffer(buffer);
    auto *dir_lights = dir_lights_buffer.map<hlsl::DirLight>();
    ranges::copy(cfg.dir_lights->values(), dir_lights);
  });

  auto materials_buffer = rg.get_buffer(rcs.materials_buffer);
  auto *materials = materials_buffer.map<hlsl::Material>();
  ranges::copy(cfg.materials, materials);
}

static auto setup_upload_data_pass(Device &device, RenderGraph::Builder &rgb,
                                   const UploadDataPassConfig &cfg)
    -> UploadDataPassOutput {
  auto pass = rgb.create_pass();
  pass.set_desc("Upload data pass");

  UploadDataPassResources rcs = {};

  rcs.transform_matrix_buffer = pass.create_buffer(
      {
          REN_SET_DEBUG_NAME("Transform matrix buffer"),
          .heap = BufferHeap::Upload,
          .size = sizeof(hlsl::model_matrix_t) * cfg.mesh_insts->size(),
      },
      VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_NONE);

  rcs.normal_matrix_buffer = pass.create_buffer(
      {
          REN_SET_DEBUG_NAME("Normal matrix buffer"),
          .heap = BufferHeap::Upload,
          .size = sizeof(hlsl::normal_matrix_t) * cfg.mesh_insts->size(),
      },
      VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_NONE);

  auto num_dir_lights = cfg.dir_lights->size();
  if (num_dir_lights > 0) {
    rcs.dir_lights_buffer = pass.create_buffer(
        {
            REN_SET_DEBUG_NAME("Dir lights buffer"),
            .heap = BufferHeap::Upload,
            .size = sizeof(hlsl::DirLight) * num_dir_lights,
        },
        VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_NONE);
  }

  rcs.materials_buffer = pass.create_buffer(
      {
          REN_SET_DEBUG_NAME("Materials buffer"),
          .heap = BufferHeap::Upload,
          .size = cfg.materials.size_bytes(),
      },
      VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_NONE);

  pass.set_callback([&device, cfg, rcs](CommandBuffer &cmd, RenderGraph &rg) {
    run_upload_data_pass(device, cmd, rg, cfg, rcs);
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

  const DenseSlotMap<Mesh> *meshes;
  std::span<const GraphicsPipelineRef> material_pipelines;
  const DenseSlotMap<MeshInst> *mesh_insts;

  VkDescriptorSet persistent_set;

  PipelineLayoutRef pipeline_layout;

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
  RGBufferID global_data_buffer;
  VkDescriptorSet global_set;
};

struct ColorPassOutput {
  RGTextureID rt;
};

static void run_color_pass(Device &device, CommandBuffer &cmd, RenderGraph &rg,
                           const ColorPassConfig &cfg,
                           const ColorPassResources &rcs) {
  cmd.begin_rendering(rg.get_texture_handle(rcs.rt),
                      rg.get_texture_handle(rcs.dst));
  cmd.set_viewport({.width = float(cfg.width),
                    .height = float(cfg.height),
                    .maxDepth = 1.0f});
  cmd.set_scissor_rect({.extent = {cfg.width, cfg.height}});

  auto global_data_buffer = rg.get_buffer_handle(rcs.global_data_buffer);
  auto *global_data =
      device.get_buffer(global_data_buffer).map<hlsl::GlobalData>();
  *global_data = {
      .proj_view = cfg.proj * cfg.view,
      .eye = cfg.eye,
      .num_dir_lights = cfg.num_dir_lights,
  };

  auto transform_matrix_buffer =
      rg.get_buffer_handle(cfg.transform_matrix_buffer);
  auto normal_matrix_buffer = rg.get_buffer_handle(cfg.normal_matrix_buffer);
  auto dir_lights_buffer = cfg.dir_lights_buffer.map(
      [&](RGBufferID buffer) { return rg.get_buffer_handle(buffer); });
  auto materials_buffer = rg.get_buffer_handle(cfg.materials_buffer);

  auto global_set = [&] {
    auto set = rg.allocate_descriptor_set(
        cfg.pipeline_layout.desc->set_layouts[hlsl::GLOBAL_SET]);
    set.add_buffer(hlsl::GLOBAL_DATA_SLOT, global_data_buffer)
        .add_buffer(hlsl::MODEL_MATRICES_SLOT, transform_matrix_buffer)
        .add_buffer(hlsl::NORMAL_MATRICES_SLOT, normal_matrix_buffer)
        .add_buffer(hlsl::MATERIALS_SLOT, materials_buffer);

    dir_lights_buffer.map([&](Handle<Buffer> buffer) {
      set.add_buffer(hlsl::DIR_LIGHTS_SLOT, buffer);
    });

    return set.write();
  }();

  std::array descriptor_sets = {cfg.persistent_set, global_set};
  cmd.bind_descriptor_sets(VK_PIPELINE_BIND_POINT_GRAPHICS, cfg.pipeline_layout,
                           0, descriptor_sets);

  for (const auto &&[i, mesh_inst] : enumerate(cfg.mesh_insts->values())) {
    const auto &mesh = get_mesh(*cfg.meshes, mesh_inst.mesh);
    auto material = mesh_inst.material;

    cmd.bind_graphics_pipeline(cfg.material_pipelines[material]);

    const auto &buffer = device.get_buffer(mesh.vertex_buffer);
    auto address = buffer.address;
    auto positions_offset = mesh.attribute_offsets[MESH_ATTRIBUTE_POSITIONS];
    auto normals_offset = mesh.attribute_offsets[MESH_ATTRIBUTE_NORMALS];
    auto colors_offset = mesh.attribute_offsets[MESH_ATTRIBUTE_COLORS];
    auto uvs_offset = mesh.attribute_offsets[MESH_ATTRIBUTE_UVS];
    hlsl::PushConstants pcs = {
        .matrix_index = unsigned(i),
        .material_index = material,
        .positions = address + positions_offset,
        .normals = address + normals_offset,
        .colors =
            (colors_offset != ATTRIBUTE_UNUSED) ? address + colors_offset : 0,
        .uvs = (uvs_offset != ATTRIBUTE_UNUSED) ? address + uvs_offset : 0,
    };
    cmd.set_push_constants(
        cfg.pipeline_layout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, pcs);

    cmd.bind_index_buffer({
        .buffer = device.get_buffer(mesh.index_buffer),
        .type = mesh.index_format,
    });
    cmd.draw_indexed({
        .num_indices = mesh.num_indices,
    });
  }

  cmd.end_rendering();
}

static auto setup_color_pass(Device &device, RenderGraph::Builder &rgb,
                             const ColorPassConfig &cfg) -> ColorPassOutput {
  auto pass = rgb.create_pass();
  pass.set_desc("Color pass");

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

  rcs.global_data_buffer = pass.create_buffer(
      {
          REN_SET_DEBUG_NAME("Global data UBO"),
          .heap = BufferHeap::Upload,
          .size = sizeof(hlsl::GlobalData),
      },
      VK_ACCESS_2_UNIFORM_READ_BIT,
      VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);

  rcs.rt = pass.create_texture(
      {
          REN_SET_DEBUG_NAME("Color buffer"),
          .format = cfg.color_format,
          .width = cfg.width,
          .height = cfg.height,
      },
      VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

  rcs.dst = pass.create_texture(
      {
          REN_SET_DEBUG_NAME("Depth buffer"),
          .format = cfg.depth_format,
          .width = cfg.width,
          .height = cfg.height,
      },
      VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
          VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
      VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
          VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
      VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

  pass.set_callback([&device, cfg, rcs](CommandBuffer &cmd, RenderGraph &rg) {
    run_color_pass(device, cmd, rg, cfg, rcs);
  });

  return {
      .rt = rcs.rt,
  };
}

void Scene::draw(Swapchain &swapchain) {
  RenderGraph::Builder rgb;

  auto uploaded_vertex_buffers =
      m_staged_vertex_buffers | map([&](Handle<Buffer> buffer) {
        return rgb.import_buffer(buffer, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                 VK_PIPELINE_STAGE_2_COPY_BIT);
      }) |
      ranges::to<Vector>();
  m_staged_vertex_buffers.clear();

  auto uploaded_index_buffers =
      m_staged_index_buffers | map([&](Handle<Buffer> buffer) {
        return rgb.import_buffer(buffer, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                 VK_PIPELINE_STAGE_2_COPY_BIT);
      }) |
      ranges::to<Vector>();
  m_staged_index_buffers.clear();

  auto uploaded_textures =
      m_staged_textures | map([&](Handle<Texture> texture) {
        return rgb.import_texture(texture, VK_ACCESS_2_TRANSFER_READ_BIT,
                                  VK_PIPELINE_STAGE_2_BLIT_BIT,
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

    // Post-process
    auto pp = rgb.create_pass();
    pp.set_desc("Post-process pass");
    auto pprt = pp.write_texture(rt,
                                 VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                                     VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                 VK_IMAGE_LAYOUT_GENERAL);
    // rgb.set_desc(pprt, "Post-processed color buffer");
    pp.set_callback([](CommandBuffer &cmd, RenderGraph &rg) {});

    rgb.present(swapchain, pprt, m_device->createBinarySemaphore(),
                m_device->createBinarySemaphore());

    auto rg = rgb.build(*m_device, m_frame_arena);

    rg.execute(*m_device, m_descriptor_set_allocator, m_cmd_allocator);
  }

  next_frame();
}

} // namespace ren
