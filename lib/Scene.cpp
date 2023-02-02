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

      m_material_allocator(*m_device),

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

void Scene::setOutputSize(unsigned width, unsigned height) {
  m_output_width = width;
  m_output_height = height;
}

MeshID Scene::create_mesh(const MeshDesc &desc) {
  std::array<std::span<const std::byte>, MESH_ATTRIBUTE_COUNT>
      upload_attributes;
  upload_attributes[MESH_ATTRIBUTE_POSITIONS] =
      std::as_bytes(std::span(desc.positions, desc.num_vertices));
  upload_attributes[MESH_ATTRIBUTE_COLORS] = std::as_bytes(
      std::span(desc.colors, desc.colors ? desc.num_vertices : 0));

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
    case MESH_ATTRIBUTE_POSITIONS: {
      return sizeof(glm::vec3);
    }
    case MESH_ATTRIBUTE_COLORS: {
      return sizeof(hlsl::color_t);
    }
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
    case MESH_ATTRIBUTE_COLORS: {
      auto colors =
          reinterpret_span<const glm::vec3>(data) | map(hlsl::encode_color);
      m_resource_uploader.stage_data(colors, mesh.vertex_allocation, offset);
      offset += size_bytes(colors);
    } break;
    }
  }

  auto indices = std::span(desc.indices, desc.num_indices);
  m_resource_uploader.stage_data(indices, mesh.index_allocation);

  return get_mesh_id(key);
}

void Scene::destroy_mesh(MeshID id) {
  auto key = get_mesh_key(id);
  auto it = m_meshes.find(key);
  assert(it != m_meshes.end() and "Unknown mesh");
  auto &mesh = it->second;
  m_vertex_buffer_pool.free(mesh.vertex_allocation);
  m_index_buffer_pool.free(mesh.index_allocation);
  m_meshes.erase(key);
}

MaterialID Scene::create_material(const MaterialDesc &desc) {
  auto pipeline = [&] {
    auto pipeline = m_compiler.get_material_pipeline(desc);
    if (pipeline) {
      return *pipeline;
    }
    return m_compiler.compile_material_pipeline({
        .material = desc,
        .layout = m_pipeline_layout,
        .rt_format = m_rt_format,
    });
  }();

  auto &&[key, material] = m_materials.emplace(Material{
      .pipeline = std::move(pipeline),
      .index = m_material_allocator.allocate(desc, m_resource_uploader),
  });

  return get_material_id(key);
}

void Scene::destroy_material(MaterialID id) {
  auto key = get_material_key(id);
  auto it = m_materials.find(key);
  assert(it != m_materials.end() and "Unknown material");
  auto &material = it->second;
  m_material_allocator.free(material.index);
  m_materials.erase(it);
}

void Scene::set_camera(const CameraDesc &desc) noexcept {
  auto pos = glm::make_vec3(desc.position);
  auto fwd = glm::make_vec3(desc.forward);
  auto up = glm::make_vec3(desc.up);
  m_camera.view = glm::lookAt(pos, pos + fwd, up);

  float ar = float(m_output_width) / float(m_output_height);
  switch (desc.projection) {
  case REN_PROJECTION_PERSPECTIVE: {
    float fov = desc.perspective.hfov / ar;
    m_camera.proj = infinitePerspectiveRH_ReverseZ(fov, ar, 0.1f);
    break;
  }
  case REN_PROJECTION_ORTHOGRAPHIC: {
    float width = desc.orthographic.width;
    float height = width / ar;
    m_camera.proj = orthoRH_ReverseZ(width, height, 0.1f, 100.0f);
    break;
  }
  }
}

auto Scene::create_model(const ModelDesc &desc) -> MeshInstanceID {
  return get_model_id(m_models.insert({
      .mesh = desc.mesh,
      .material = desc.material,
      .matrix = glm::mat4(1.0f),
  }));
}

void Scene::destroy_model(MeshInstanceID model) {
  m_models.erase(get_model_key(model));
}

auto Scene::get_mesh(MeshID mesh) const -> const Mesh & {
  auto key = get_mesh_key(mesh);
  assert(m_meshes.contains(key) && "Unknown mesh");
  return m_meshes[key];
}

auto Scene::get_mesh(MeshID mesh) -> Mesh & {
  auto key = get_mesh_key(mesh);
  assert(m_meshes.contains(key) && "Unknown mesh");
  return m_meshes[key];
}

auto Scene::get_material(MaterialID material) const -> const Material & {
  auto key = get_material_key(material);
  assert(m_materials.contains(key) && "Unknown material");
  return m_materials[key];
}

auto Scene::get_material(MaterialID material) -> Material & {
  auto key = get_material_key(material);
  assert(m_materials.contains(key) && "Unknown material");
  return m_materials[key];
}

auto Scene::get_model(MeshInstanceID model) const -> const Model & {
  auto key = get_model_key(model);
  assert(m_models.contains(key) && "Unknown model");
  return m_models[key];
}

auto Scene::get_model(MeshInstanceID model) -> Model & {
  auto key = get_model_key(model);
  assert(m_models.contains(key) && "Unknown model");
  return m_models[key];
}

void Scene::set_model_matrix(MeshInstanceID model,
                             const glm::mat4 &matrix) noexcept {
  get_model(model).matrix = matrix;
}

void Scene::draw(Swapchain &swapchain, unsigned width, unsigned height) {
  m_output_width = width;
  m_output_height = height;

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
  auto draw = rgb.addNode();
  draw.setDesc("Color pass");

  auto rt = draw.addOutput(
      RGTextureDesc{
          .format = m_rt_format,
          .width = m_output_width,
          .height = m_output_height,
      },
      VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
  rgb.setDesc(rt, "Color buffer");

  auto virtual_global_cbuffer = draw.add_output(
      RGBufferDesc{
          .heap = BufferHeap::Upload,
          .size = unsigned(sizeof(hlsl::GlobalData)),
      },
      VK_ACCESS_2_UNIFORM_READ_BIT,
      VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
  rgb.set_desc(virtual_global_cbuffer, "Global cbuffer");

  auto virtual_matrix_buffer = draw.add_output(
      RGBufferDesc{
          .heap = BufferHeap::Upload,
          .size = unsigned(sizeof(hlsl::model_matrix_t) * m_models.size()),
      },
      VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
      VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT);
  rgb.set_desc(virtual_matrix_buffer, "Model matrix buffer");

  draw.setCallback([this, rt, virtual_matrix_buffer, virtual_global_cbuffer](
                       CommandBuffer &cmd, RenderGraph &rg) {
    cmd.begin_rendering(rg.getTexture(rt));
    cmd.set_viewport({.width = float(m_output_width),
                      .height = float(m_output_height),
                      .maxDepth = 1.0f});
    cmd.set_scissor_rect({.extent = {m_output_width, m_output_height}});

    // FIXME: since null descriptors are not core in Vulkan, have to skip
    // binding empty matrix buffer
    if (m_models.empty()) {
      cmd.end_rendering();
      return;
    }

    BufferRef global_cbuffer = rg.get_buffer(virtual_global_cbuffer);
    *global_cbuffer.map<hlsl::GlobalData>() = {.proj_view = m_camera.proj *
                                                            m_camera.view};

    BufferRef matrix_buffer = rg.get_buffer(virtual_matrix_buffer);
    auto *matrices = matrix_buffer.map<hlsl::model_matrix_t>();

    auto scene_descriptor_set =
        m_descriptor_set_allocator.allocate(get_global_descriptor_set_layout());

    auto global_ub_descriptor = global_cbuffer.get_descriptor();
    auto matrix_buffer_descriptor = matrix_buffer.get_descriptor();

    std::array write_configs = {
        VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = scene_descriptor_set,
            .dstBinding = hlsl::GLOBAL_CB_SLOT,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &global_ub_descriptor,
        },
        VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = scene_descriptor_set,
            .dstBinding = hlsl::MATRICES_SLOT,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &matrix_buffer_descriptor,
        },
    };

    m_device->write_descriptor_sets(write_configs);

    std::array descriptor_sets = {
        m_persistent_descriptor_set,
        scene_descriptor_set,
    };

    cmd.bind_descriptor_sets(VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline_layout,
                             0, descriptor_sets);

    for (const auto &&[i, model] :
         ranges::views::enumerate(m_models.values())) {
      matrices[i] = model.matrix;

      const auto &mesh = get_mesh(model.mesh);
      const auto &material = get_material(model.material);

      cmd.bind_graphics_pipeline(material.pipeline);

      const auto &buffer = mesh.vertex_allocation;
      auto address = buffer.desc.address;
      auto positions_offset = mesh.attribute_offsets[MESH_ATTRIBUTE_POSITIONS];
      auto colors_offset = mesh.attribute_offsets[MESH_ATTRIBUTE_COLORS];
      hlsl::PushConstants pcs = {
          .vertex =
              {
                  .matrix_index = unsigned(i),
                  .positions = address + positions_offset,
                  .colors = (colors_offset != ATTRIBUTE_UNUSED)
                                ? address + colors_offset
                                : 0,
              },
          .fragment = {.material_index = material.index},
      };
      cmd.set_push_constants(m_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                             pcs.vertex, offsetof(decltype(pcs), vertex));
      cmd.set_push_constants(m_pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                             pcs.fragment, offsetof(decltype(pcs), fragment));

      cmd.bind_index_buffer(mesh.index_allocation, mesh.index_format);
      cmd.draw_indexed(mesh.num_indices);
    }

    cmd.end_rendering();
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
