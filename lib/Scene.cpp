#include "Scene.hpp"
#include "Camera.inl"
#include "CommandAllocator.hpp"
#include "Descriptors.hpp"
#include "Device.hpp"
#include "Errors.hpp"
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

static MeshInstanceMap::key_type get_model_key(RenMeshInst model) {
  return std::bit_cast<MeshInstanceMap::key_type>(model - 1);
}

static RenMeshInst get_model_id(MeshInstanceMap::key_type model_key) {
  return std::bit_cast<RenMeshInst>(std::bit_cast<RenMeshInst>(model_key) + 1);
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

static auto get_model(const MeshInstanceMap &m_models, RenMeshInst model)
    -> const Model & {
  auto key = get_model_key(model);
  assert(m_models.contains(key) && "Unknown model");
  return m_models[key];
}

static auto get_model(MeshInstanceMap &m_models, RenMeshInst model) -> Model & {
  auto key = get_model_key(model);
  assert(m_models.contains(key) && "Unknown model");
  return m_models[key];
}

auto Scene::get_dir_light(RenDirLight light) const -> const hlsl::DirLight & {
  auto key = get_dir_light_key(light);
  assert(m_dir_lights.contains(key) && "Unknown light");
  return m_dir_lights[key];
}

auto Scene::get_dir_light(RenDirLight light) -> hlsl::DirLight & {
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

      m_vertex_buffer_pool(
          m_device,
          BufferDesc{
              .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
              .heap = BufferHeap::Device,
              .size = 1 << 26,
          }),
      m_index_buffer_pool(m_device,
                          BufferDesc{
                              .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                       VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                              .heap = BufferHeap::Device,
                              .size = 1 << 22,
                          }),

      m_resource_uploader(*m_device),

      m_compiler(*m_device, &m_asset_loader),

      m_descriptor_set_allocator(*m_device),

      m_cmd_allocator(*m_device),

      m_pipeline_layout(
          reflect_material_pipeline_layout(*m_device, m_asset_loader)) {}

void Scene::next_frame() {
  m_device->next_frame();
  m_cmd_allocator.next_frame();
  m_descriptor_set_allocator.next_frame();
  m_resource_uploader.next_frame();
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

  auto vertex_allocation_size =
      desc.num_vertices *
      ranges::accumulate(used_attributes | map(get_mesh_attribute_size), 0);
  auto index_allocation_size = desc.num_indices * sizeof(unsigned);

  auto &&[key, mesh] = m_meshes.emplace(Mesh{
      .vertex_allocation =
          m_vertex_buffer_pool.allocate(vertex_allocation_size),
      .index_allocation = m_index_buffer_pool.allocate(index_allocation_size),
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
      m_resource_uploader.stage_data(positions, mesh.vertex_allocation, offset);
      offset += size_bytes(positions);
    } break;
    case MESH_ATTRIBUTE_NORMALS: {
      auto normals =
          reinterpret_span<const glm::vec3>(data) | map(hlsl::encode_normal);
      m_resource_uploader.stage_data(normals, mesh.vertex_allocation, offset);
      offset += size_bytes(normals);
    } break;
    case MESH_ATTRIBUTE_COLORS: {
      auto colors =
          reinterpret_span<const glm::vec4>(data) | map(hlsl::encode_color);
      m_resource_uploader.stage_data(colors, mesh.vertex_allocation, offset);
      offset += size_bytes(colors);
    } break;
    }
  }

  auto indices = std::span(desc.indices, desc.num_indices);
  m_resource_uploader.stage_data(indices, mesh.index_allocation);

  return get_mesh_id(key);
}

RenMaterial Scene::create_material(const RenMaterialDesc &desc) {
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

  return get_material_id(key);
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

auto Scene::create_model(const RenMeshInstDesc &desc) -> RenMeshInst {
  return get_model_id(m_models.insert({
      .mesh = desc.mesh,
      .material = desc.material,
      .matrix = glm::mat4(1.0f),
  }));
}

void Scene::destroy_model(RenMeshInst model) {
  m_models.erase(get_model_key(model));
}

void Scene::set_model_matrix(RenMeshInst model,
                             const glm::mat4 &matrix) noexcept {
  get_model(m_models, model).matrix = matrix;
}

auto Scene::create_dir_light(const RenDirLightDesc &desc) -> RenDirLight {
  assert(m_dir_lights.size() < hlsl::MAX_DIRECTIONAL_LIGHTS);
  auto key = m_dir_lights.insert(hlsl::DirLight{
      .color = glm::make_vec3(desc.color),
      .illuminance = desc.illuminance,
      .origin = glm::make_vec3(desc.origin),
  });
  return get_dir_light_id(key);
};

void Scene::destroy_dir_light(RenDirLight light) {
  auto key = get_dir_light_key(light);
  assert(m_dir_lights.contains(key) && "Unknown light");
  m_dir_lights.erase(key);
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
  const SlotMap<Model> *models;
  const SlotMap<hlsl::DirLight> *dir_lights;

  VkDescriptorSet persistent_set;
  VkDescriptorSet scene_set;

  PipelineLayoutRef pipeline_layout;
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
  cmd.begin_rendering(rg.getTexture(rcs.rt), rg.getTexture(rcs.dst));
  cmd.set_viewport({.width = float(cfg.width),
                    .height = float(cfg.height),
                    .maxDepth = 1.0f});
  cmd.set_scissor_rect({.extent = {cfg.width, cfg.height}});

  [&] {
    if (cfg.models->empty()) {
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
    ranges::transform(cfg.models->values(), transform_matrices,
                      [](const auto &model) { return model.matrix; });

    BufferRef normal_matrix_buffer = rg.get_buffer(rcs.normal_matrix_buffer);
    auto *normal_matrices = normal_matrix_buffer.map<hlsl::normal_matrix_t>();
    ranges::transform(
        cfg.models->values(), normal_matrices, [](const auto &model) {
          return glm::transpose(glm::inverse(glm::mat3(model.matrix)));
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

    for (const auto &&[i, model] :
         ranges::views::enumerate(cfg.models->values())) {
      const auto &mesh = get_mesh(*cfg.meshes, model.mesh);
      const auto &material = get_material(*cfg.materials, model.material);

      cmd.bind_graphics_pipeline(material.pipeline);

      const auto &buffer = mesh.vertex_allocation;
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

      cmd.bind_index_buffer(mesh.index_allocation, mesh.index_format);
      cmd.draw_indexed(mesh.num_indices);
    }
  }();

  cmd.end_rendering();
}

static auto setup_color_pass(Device &device, RenderGraph::Builder &rgb,
                             const ColorPassConfig &cfg) -> ColorPassOutput {
  auto draw = rgb.addNode();
  draw.setDesc("Color pass");

  ColorPassResources rcs = {};

  rcs.rt = draw.addOutput(
      RGTextureDesc{
          .format = cfg.color_format,
          .width = cfg.width,
          .height = cfg.height,
      },
      VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
  rgb.setDesc(rcs.rt, "Color buffer");

  rcs.dst = draw.addOutput(
      RGTextureDesc{
          .format = cfg.depth_format,
          .width = cfg.width,
          .height = cfg.height,
      },
      VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
          VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
      VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
          VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT);
  rgb.setDesc(rcs.dst, "Depth buffer");

  rcs.global_data_buffer = draw.add_output(
      RGBufferDesc{
          .heap = BufferHeap::Upload,
          .size = unsigned(sizeof(hlsl::GlobalData)),
      },
      VK_ACCESS_2_UNIFORM_READ_BIT,
      VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
  rgb.set_desc(rcs.global_data_buffer, "Global data UBO");

  rcs.transform_matrix_buffer = draw.add_output(
      RGBufferDesc{
          .heap = BufferHeap::Upload,
          .size = unsigned(sizeof(hlsl::model_matrix_t) * cfg.models->size()),
      },
      VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
      VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT);
  rgb.set_desc(rcs.transform_matrix_buffer, "Transform matrix buffer");

  rcs.normal_matrix_buffer = draw.add_output(
      RGBufferDesc{
          .heap = BufferHeap::Upload,
          .size = unsigned(sizeof(hlsl::normal_matrix_t) * cfg.models->size()),
      },
      VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
      VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT);
  rgb.set_desc(rcs.normal_matrix_buffer, "Normal matrix buffer");

  rcs.lights_buffer = draw.add_output(
      RGBufferDesc{
          .heap = BufferHeap::Upload,
          .size = unsigned(sizeof(hlsl::Lights)),
      },
      VK_ACCESS_2_UNIFORM_READ_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
  rgb.set_desc(rcs.lights_buffer, "Lights buffer");

  draw.setCallback([&device, cfg, rcs](CommandBuffer &cmd, RenderGraph &rg) {
    run_color_pass(device, cmd, rg, cfg, rcs);
  });

  return {
      .rt = rcs.rt,
  };
}

void Scene::draw(Swapchain &swapchain) {
  m_resource_uploader.upload_data(m_cmd_allocator);

  bool update_persistent_descriptor_pool = false;
  if (const auto &materials_buffer = m_material_allocator.get_buffer();
      materials_buffer != m_materials_buffer) {
    m_materials_buffer = materials_buffer;
    update_persistent_descriptor_pool = true;
  }

  if (update_persistent_descriptor_pool) {
    auto [new_pool, new_set] = m_device->allocate_descriptor_set(
        get_persistent_descriptor_set_layout());
    m_persistent_descriptor_pool = std::move(new_pool);
    m_persistent_descriptor_set = std::move(new_set);
    auto descriptor = m_materials_buffer.get_descriptor();
    m_device->write_descriptor_set({
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = m_persistent_descriptor_set,
        .dstBinding = hlsl::MATERIALS_SLOT,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &descriptor,
    });
  }

  RenderGraph::Builder rgb(*m_device);
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
          .models = &m_models,
          .dir_lights = &m_dir_lights,
          .persistent_set = m_persistent_descriptor_set,
          .scene_set = m_descriptor_set_allocator.allocate(
              get_global_descriptor_set_layout()),
          .pipeline_layout = m_pipeline_layout,
      });

  // Post-process
  auto pp = rgb.addNode();
  pp.setDesc("Post-process pass");
  auto pprt = pp.addWriteInput(rt,
                               VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                                   VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                               VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
  rgb.setDesc(pprt, "Post-processed color buffer");
  pp.setCallback([](CommandBuffer &cmd, RenderGraph &rg) {});

  rgb.present(swapchain, pprt);

  auto rg = rgb.build();

  rg.execute(m_cmd_allocator);

  next_frame();
}

} // namespace ren
