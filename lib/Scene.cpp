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

static MaterialMap::key_type get_material_key(RenMaterial material) {
  return std::bit_cast<MaterialMap::key_type>(material - 1);
}

static RenMaterial get_material_id(MaterialMap::key_type material_key) {
  return std::bit_cast<RenMaterial>(std::bit_cast<RenMaterial>(material_key) +
                                    1);
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

static auto get_material(const MaterialMap &m_materials, RenMaterial material)
    -> const Material & {
  auto key = get_material_key(material);
  assert(m_materials.contains(key) && "Unknown material");
  return m_materials[key];
}

static auto get_material(MaterialMap &m_materials, RenMaterial material)
    -> Material & {
  auto key = get_material_key(material);
  assert(m_materials.contains(key) && "Unknown material");
  return m_materials[key];
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

void reflect_descriptor_set_layouts(const ReflectionModule &vs,
                                    const ReflectionModule &fs,
                                    Vector<DescriptorSetLayoutDesc> &sets) {
  Vector<DescriptorBindingReflection> shader_bindings;
  for (const auto *shader : {&vs, &fs}) {
    shader->get_bindings(shader_bindings);
    for (const auto &[set, shader_binding] : shader_bindings) {
      if (set >= sets.size()) {
        sets.resize(set + 1);
      }
      auto &set_bindings = sets[set].bindings;
      auto it = ranges::find_if(set_bindings,
                                [binding = shader_binding.binding](
                                    const DescriptorBinding &set_binding) {
                                  return set_binding.binding == binding;
                                });
      if (it != set_bindings.end()) {
        auto &set_binding = *it;
        set_binding.stages |= shader->get_shader_stage();
        assert(set_binding.binding == shader_binding.binding);
        assert(set_binding.type == shader_binding.type);
        assert(set_binding.count == shader_binding.count);
      } else {
        set_bindings.push_back(shader_binding);
      }
    }
  }
}

auto reflect_material_pipeline_layout(Device &device, const AssetLoader &loader)
    -> PipelineLayout {
  Vector<std::byte> buffer;
  loader.load_file("ReflectionVertexShader.spv", buffer);
  ReflectionModule vs(buffer);
  loader.load_file("ReflectionFragmentShader.spv", buffer);
  ReflectionModule fs(buffer);

  Vector<DescriptorSetLayoutDesc> set_layout_descs;
  reflect_descriptor_set_layouts(vs, fs, set_layout_descs);
  assert(set_layout_descs.size() == 2);
  set_layout_descs[hlsl::PERSISTENT_SET].flags |=
      VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;

  PipelineLayoutDesc layout_desc = {
      .set_layouts = set_layout_descs |
                     map([&](const DescriptorSetLayoutDesc &desc) {
                       return device.create_descriptor_set_layout(desc);
                     }) |
                     ranges::to<Vector>,
      .push_constants =
          {
              {.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
               .offset = offsetof(hlsl::PushConstants, vertex),
               .size = sizeof(hlsl::PushConstants::vertex)},
              {.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
               .offset = offsetof(hlsl::PushConstants, fragment),
               .size = sizeof(hlsl::PushConstants::fragment)},
          },
  };

  return device.create_pipeline_layout(std::move(layout_desc));
}

} // namespace

Scene::Scene(Device &device)
    : m_device(&device),

      m_asset_loader([&] {
        AssetLoader asset_loader;
        asset_loader.add_search_directory(c_assets_directory);
        return asset_loader;
      }()),

      m_compiler(*m_device, &m_asset_loader),

      m_descriptor_set_allocator(*m_device),

      m_cmd_allocator(*m_device),

      m_pipeline_layout(
          reflect_material_pipeline_layout(*m_device, m_asset_loader)) {}

void Scene::next_frame() {
  m_device->next_frame();
  m_cmd_allocator.next_frame();
  m_descriptor_set_allocator.next_frame();
}

RenMesh Scene::create_mesh(const RenMeshDesc &desc) {
  std::array<std::span<const std::byte>, MESH_ATTRIBUTE_COUNT>
      upload_attributes;
  upload_attributes[MESH_ATTRIBUTE_POSITIONS] =
      std::as_bytes(std::span(desc.positions, desc.num_vertices));
  upload_attributes[MESH_ATTRIBUTE_COLORS] = std::as_bytes(
      std::span(desc.colors, desc.colors ? desc.num_vertices : 0));
  if (!desc.normals) {
    todo("Normal generation not implemented!");
  }
  upload_attributes[MESH_ATTRIBUTE_NORMALS] =
      std::as_bytes(std::span(desc.normals, desc.num_vertices));
  if (desc.tangents) {
    todo("Normal mapping not implemented!");
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
    }
  };

  unsigned vertex_buffer_size =
      desc.num_vertices *
      ranges::accumulate(used_attributes | map(get_mesh_attribute_size), 0);
  unsigned index_buffer_size = desc.num_indices * sizeof(unsigned);

  auto &&[key, mesh] = m_meshes.emplace(Mesh{
      .vertex_buffer = m_device->create_buffer({
          .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
          .heap = BufferHeap::Device,
          .size = vertex_buffer_size,
      }),
      .index_buffer = m_device->create_buffer({
          .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                   VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
          .heap = BufferHeap::Device,
          .size = index_buffer_size,
      }),
      .num_vertices = desc.num_vertices,
      .num_indices = desc.num_indices,
      .index_format = VK_INDEX_TYPE_UINT32,
  });

  mesh.attribute_offsets.fill(ATTRIBUTE_UNUSED);

  unsigned offset = 0;
  for (auto mesh_attribute : used_attributes) {
    mesh.attribute_offsets[mesh_attribute] = offset;
    auto data = upload_attributes[mesh_attribute];
    switch (mesh_attribute) {
    default:
      assert(!"Unknown mesh attribute");
    case MESH_ATTRIBUTE_POSITIONS: {
      auto positions = reinterpret_span<const glm::vec3>(data);
      m_resource_uploader.stage_buffer(*m_device, positions, mesh.vertex_buffer,
                                       offset);
      offset += size_bytes(positions);
    } break;
    case MESH_ATTRIBUTE_NORMALS: {
      auto normals =
          reinterpret_span<const glm::vec3>(data) | map(hlsl::encode_normal);
      m_resource_uploader.stage_buffer(*m_device, normals, mesh.vertex_buffer,
                                       offset);
      offset += size_bytes(normals);
    } break;
    case MESH_ATTRIBUTE_COLORS: {
      auto colors =
          reinterpret_span<const glm::vec4>(data) | map(hlsl::encode_color);
      m_resource_uploader.stage_buffer(*m_device, colors, mesh.vertex_buffer,
                                       offset);
      offset += size_bytes(colors);
    } break;
    }
  }

  auto indices = std::span(desc.indices, desc.num_indices);
  m_resource_uploader.stage_buffer(*m_device, indices, mesh.index_buffer);

  if (!mesh.vertex_buffer.desc.ptr) {
    m_staged_vertex_buffers.push_back(mesh.vertex_buffer);
  }

  if (!mesh.index_buffer.desc.ptr) {
    m_staged_index_buffers.push_back(mesh.index_buffer);
  }

  return get_mesh_id(key);
}

auto Scene::create_image(const RenImageDesc &desc) -> RenImage {
  auto image = static_cast<RenImage>(m_images.size());
  auto format = getVkFormat(desc.format);
  m_images.push_back(m_device->create_texture({
      .type = VK_IMAGE_TYPE_2D,
      .format = format,
      .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
               VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
      .width = desc.width,
      .height = desc.height,
      .mip_levels = get_mip_level_count(desc.width, desc.height),
  }));
  auto &texture = m_images[image];
  auto image_size = desc.width * desc.height * get_format_size(format);
  m_resource_uploader.stage_texture(
      *m_device,
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
      return m_compiler.compile_material_pipeline({
          .material = desc,
          .layout = m_pipeline_layout,
          .rt_format = m_rt_format,
          .depth_format = m_depth_format,
      });
    }();

    auto &&[key, material] = m_materials.emplace(Material{
        .pipeline = std::move(pipeline),
        .index =
            m_material_allocator.allocate(*m_device, desc, m_resource_uploader),
    });

    *(out++) = get_material_id(key);
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
  assert(m_dir_lights.size() < hlsl::MAX_DIRECTIONAL_LIGHTS);
  for (const auto &desc : descs) {
    auto key = m_dir_lights.insert(hlsl::DirLight{
        .color = glm::make_vec3(desc.color),
        .illuminance = desc.illuminance,
        .origin = glm::make_vec3(desc.origin),
    });
    *(out++) = get_dir_light_id(key);
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

struct ColorPassConfig {
  VkFormat color_format;
  VkFormat depth_format;
  unsigned width;
  unsigned height;

  glm::mat4 proj;
  glm::mat4 view;
  glm::vec3 eye;

  const SlotMap<Mesh> *meshes;
  const SlotMap<Material> *materials;
  const SlotMap<MeshInst> *mesh_insts;
  const SlotMap<hlsl::DirLight> *dir_lights;

  VkDescriptorSet persistent_set;
  VkDescriptorSet scene_set;

  PipelineLayoutRef pipeline_layout;

  std::span<const RGBufferID> uploaded_vertex_buffers;
  std::span<const RGBufferID> uploaded_index_buffers;
  std::span<const RGTextureID> uploaded_textures;
  RGBufferID materials_buffer;
};

struct ColorPassResources {
  RGTextureID rt;
  RGTextureID dst;
  RGBufferID global_data_buffer;
  RGBufferID transform_matrix_buffer;
  RGBufferID normal_matrix_buffer;
  RGBufferID lights_buffer;
};

struct ColorPassOutput {
  RGTextureID rt;
};

static void run_color_pass(Device &device, CommandBuffer &cmd, RenderGraph &rg,
                           const ColorPassConfig &cfg,
                           const ColorPassResources &rcs) {
  cmd.begin_rendering(rg.get_texture(rcs.rt), rg.get_texture(rcs.dst));
  cmd.set_viewport({.width = float(cfg.width),
                    .height = float(cfg.height),
                    .maxDepth = 1.0f});
  cmd.set_scissor_rect({.extent = {cfg.width, cfg.height}});

  [&] {
    if (cfg.mesh_insts->empty()) {
      return;
    }

    BufferRef global_data_buffer = rg.get_buffer(rcs.global_data_buffer);
    auto *global_data = global_data_buffer.map<hlsl::GlobalData>();
    *global_data = {
        .proj_view = cfg.proj * cfg.view,
        .eye = cfg.eye,
    };

    BufferRef transform_matrix_buffer =
        rg.get_buffer(rcs.transform_matrix_buffer);
    auto *transform_matrices =
        transform_matrix_buffer.map<hlsl::model_matrix_t>();
    ranges::transform(cfg.mesh_insts->values(), transform_matrices,
                      [](const auto &mesh_inst) { return mesh_inst.matrix; });

    BufferRef normal_matrix_buffer = rg.get_buffer(rcs.normal_matrix_buffer);
    auto *normal_matrices = normal_matrix_buffer.map<hlsl::normal_matrix_t>();
    ranges::transform(
        cfg.mesh_insts->values(), normal_matrices, [](const auto &mesh_inst) {
          return glm::transpose(glm::inverse(glm::mat3(mesh_inst.matrix)));
        });

    BufferRef lights_buffer = rg.get_buffer(rcs.lights_buffer);
    auto *lights = lights_buffer.map<hlsl::Lights>();
    lights->num_dir_lights = cfg.dir_lights->size();
    ranges::copy(cfg.dir_lights->values(), lights->dir_lights);

    auto global_ubo_descriptor = global_data_buffer.get_descriptor();
    auto transform_matrix_buffer_descriptor =
        transform_matrix_buffer.get_descriptor();
    auto normal_matrix_buffer_descriptor =
        normal_matrix_buffer.get_descriptor();
    auto lights_buffer_descriptor = lights_buffer.get_descriptor();

    std::array write_configs = {
        VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = cfg.scene_set,
            .dstBinding = hlsl::GLOBAL_CB_SLOT,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &global_ubo_descriptor,
        },
        VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = cfg.scene_set,
            .dstBinding = hlsl::MODEL_MATRICES_SLOT,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &transform_matrix_buffer_descriptor,
        },
        VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = cfg.scene_set,
            .dstBinding = hlsl::NORMAL_MATRICES_SLOT,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &normal_matrix_buffer_descriptor,
        },
        VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = cfg.scene_set,
            .dstBinding = hlsl::LIGHTS_SLOT,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &lights_buffer_descriptor,
        },
    };
    device.write_descriptor_sets(write_configs);

    std::array descriptor_sets = {cfg.persistent_set, cfg.scene_set};
    cmd.bind_descriptor_sets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                             cfg.pipeline_layout, 0, descriptor_sets);

    for (const auto &&[i, mesh_inst] :
         ranges::views::enumerate(cfg.mesh_insts->values())) {
      const auto &mesh = get_mesh(*cfg.meshes, mesh_inst.mesh);
      const auto &material = get_material(*cfg.materials, mesh_inst.material);

      cmd.bind_graphics_pipeline(material.pipeline);

      const auto &buffer = mesh.vertex_buffer;
      auto address = buffer.desc.address;
      auto positions_offset = mesh.attribute_offsets[MESH_ATTRIBUTE_POSITIONS];
      auto normals_offset = mesh.attribute_offsets[MESH_ATTRIBUTE_NORMALS];
      auto colors_offset = mesh.attribute_offsets[MESH_ATTRIBUTE_COLORS];
      hlsl::PushConstants pcs = {
          .vertex =
              {
                  .matrix_index = unsigned(i),
                  .positions = address + positions_offset,
                  .normals = address + normals_offset,
                  .colors = (colors_offset != ATTRIBUTE_UNUSED)
                                ? address + colors_offset
                                : 0,
              },
          .fragment = {.material_index = material.index},
      };
      cmd.set_push_constants(cfg.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                             pcs.vertex, offsetof(decltype(pcs), vertex));
      cmd.set_push_constants(cfg.pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                             pcs.fragment, offsetof(decltype(pcs), fragment));

      cmd.bind_index_buffer(mesh.index_buffer, mesh.index_format);
      cmd.draw_indexed(mesh.num_indices);
    }
  }();

  cmd.end_rendering();
}

static auto setup_color_pass(Device &device, RenderGraph::Builder &rgb,
                             const ColorPassConfig &cfg) -> ColorPassOutput {
  auto draw = rgb.create_pass();
  draw.set_desc("Color pass");

  ColorPassResources rcs = {};

  for (auto buffer : cfg.uploaded_vertex_buffers) {
    draw.read_buffer(buffer, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                     VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT);
  }

  for (auto buffer : cfg.uploaded_index_buffers) {
    draw.read_buffer(buffer, VK_ACCESS_2_INDEX_READ_BIT,
                     VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT);
  }

  for (auto texture : cfg.uploaded_textures) {
    draw.read_texture(texture, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }

  draw.read_buffer(cfg.materials_buffer, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);

  rcs.rt = draw.create_texture(
      {
          .format = cfg.color_format,
          .width = cfg.width,
          .height = cfg.height,
      },
      VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  rgb.set_desc(rcs.rt, "Color buffer");

  rcs.dst = draw.create_texture(
      {
          .format = cfg.depth_format,
          .width = cfg.width,
          .height = cfg.height,
      },
      VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
          VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
      VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
          VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
      VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  rgb.set_desc(rcs.dst, "Depth buffer");

  rcs.global_data_buffer = draw.create_buffer(
      {
          .heap = BufferHeap::Upload,
          .size = unsigned(sizeof(hlsl::GlobalData)),
      },
      VK_ACCESS_2_UNIFORM_READ_BIT,
      VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
  rgb.set_desc(rcs.global_data_buffer, "Global data UBO");

  rcs.transform_matrix_buffer = draw.create_buffer(
      {
          .heap = BufferHeap::Upload,
          .size =
              unsigned(sizeof(hlsl::model_matrix_t) * cfg.mesh_insts->size()),
      },
      VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
      VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT);
  rgb.set_desc(rcs.transform_matrix_buffer, "Transform matrix buffer");

  rcs.normal_matrix_buffer = draw.create_buffer(
      {
          .heap = BufferHeap::Upload,
          .size =
              unsigned(sizeof(hlsl::normal_matrix_t) * cfg.mesh_insts->size()),
      },
      VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
      VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT);
  rgb.set_desc(rcs.normal_matrix_buffer, "Normal matrix buffer");

  rcs.lights_buffer = draw.create_buffer(
      {
          .heap = BufferHeap::Upload,
          .size = unsigned(sizeof(hlsl::Lights)),
      },
      VK_ACCESS_2_UNIFORM_READ_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
  rgb.set_desc(rcs.lights_buffer, "Lights buffer");

  draw.set_callback([&device, cfg, rcs](CommandBuffer &cmd, RenderGraph &rg) {
    run_color_pass(device, cmd, rg, cfg, rcs);
  });

  return {
      .rt = rcs.rt,
  };
}

void Scene::draw(Swapchain &swapchain) {
  RenderGraph::Builder rgb;

  auto uploaded_vertex_buffers =
      m_staged_vertex_buffers | map([&](Buffer &buffer) {
        return rgb.import_buffer(std::move(buffer),
                                 VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                 VK_PIPELINE_STAGE_2_COPY_BIT);
      }) |
      ranges::to<Vector>();
  m_staged_vertex_buffers.clear();

  auto uploaded_index_buffers =
      m_staged_index_buffers | map([&](Buffer &buffer) {
        return rgb.import_buffer(std::move(buffer),
                                 VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                 VK_PIPELINE_STAGE_2_COPY_BIT);
      }) |
      ranges::to<Vector>();
  m_staged_index_buffers.clear();

  auto uploaded_textures =
      m_staged_textures | map([&](Texture &texture) {
        return rgb.import_texture(
            std::move(texture), VK_ACCESS_2_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_2_BLIT_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
      }) |
      ranges::to<Vector>();
  m_staged_textures.clear();

  auto upload_cmd = m_resource_uploader.record_upload(m_cmd_allocator);
  if (upload_cmd) {
    VkCommandBufferSubmitInfo cmd_submit = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = upload_cmd->get(),
    };
    Submit submit = {.command_buffers = {&cmd_submit, 1}};
    m_device->graphicsQueueSubmit(asSpan(submit));
  }

  {
    auto [new_pool, new_set] = m_device->allocate_descriptor_set(
        get_persistent_descriptor_set_layout());
    m_persistent_descriptor_pool = std::move(new_pool);
    m_persistent_descriptor_set = std::move(new_set);
    auto descriptor = m_material_allocator.get_buffer().get_descriptor();
    m_device->write_descriptor_set({
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = m_persistent_descriptor_set,
        .dstBinding = hlsl::MATERIALS_SLOT,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &descriptor,
    });
  }

  // Draw scene
  auto [rt] = setup_color_pass(
      *m_device, rgb,
      {
          .color_format = m_rt_format,
          .depth_format = m_depth_format,
          .width = m_viewport_width,
          .height = m_viewport_height,
          .proj = get_projection_matrix(m_camera, float(m_viewport_width) /
                                                      float(m_viewport_height)),
          .view = get_view_matrix(m_camera),
          .eye = m_camera.position,
          .meshes = &m_meshes,
          .materials = &m_materials,
          .mesh_insts = &m_mesh_insts,
          .dir_lights = &m_dir_lights,
          .persistent_set = m_persistent_descriptor_set,
          .scene_set = m_descriptor_set_allocator.allocate(
              get_global_descriptor_set_layout()),
          .pipeline_layout = m_pipeline_layout,
          .uploaded_vertex_buffers = uploaded_vertex_buffers,
          .uploaded_index_buffers = uploaded_index_buffers,
          .uploaded_textures = uploaded_textures,
          .materials_buffer = rgb.import_buffer(
              m_material_allocator.get_buffer(), VK_ACCESS_2_TRANSFER_WRITE_BIT,
              VK_PIPELINE_STAGE_2_COPY_BIT),
      });

  // Post-process
  auto pp = rgb.create_pass();
  pp.set_desc("Post-process pass");
  auto pprt = pp.write_texture(rt,
                               VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                                   VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                               VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                               VK_IMAGE_LAYOUT_GENERAL);
  rgb.set_desc(pprt, "Post-processed color buffer");
  pp.set_callback([](CommandBuffer &cmd, RenderGraph &rg) {});

  rgb.present(swapchain, pprt, m_device->createBinarySemaphore(),
              m_device->createBinarySemaphore());

  auto rg = rgb.build(*m_device);

  rg.execute(*m_device, m_cmd_allocator);

  next_frame();
}

} // namespace ren
