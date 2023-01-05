#include "Scene.hpp"
#include "Camera.inl"
#include "Device.hpp"
#include "RenderGraph.hpp"
#include "ResourceUploader.inl"
#include "Support/Array.hpp"
#include "Support/Views.hpp"
#include "hlsl/encode.hlsl"

#include <range/v3/algorithm.hpp>
#include <range/v3/range.hpp>

using namespace ren;

Scene::RenScene(Device *device)
    : m_device(device),
      m_vertex_buffer_pool(
          m_device,
          {
              .usage = BufferUsage::TransferDST | BufferUsage::DeviceAddress,
              .location = BufferLocation::Device,
              .size = 1 << 26,
          }),
      m_index_buffer_pool(
          m_device,
          {
              .usage = BufferUsage::TransferDST | BufferUsage::Index,
              .location = BufferLocation::Device,
              .size = 1 << 22,
          }),
      m_material_allocator(*m_device), m_resource_uploader(*m_device) {
  begin_frame();
}

Scene::~RenScene() { end_frame(); }

void Scene::setOutputSize(unsigned width, unsigned height) {
  m_output_width = width;
  m_output_height = height;
}

void Scene::setSwapchain(Swapchain *swapchain) { m_swapchain = swapchain; }

MeshID Scene::create_mesh(const MeshDesc &desc) {
  auto vertex_allocation_size =
      desc.num_vertices *
      (sizeof(glm::vec3) + (desc.colors ? sizeof(color_t) : 0));
  auto index_allocation_size = desc.num_indices * sizeof(unsigned);

  auto &&[key, mesh] = m_meshes.emplace(Mesh{
      .vertex_allocation =
          m_vertex_buffer_pool.allocate(vertex_allocation_size),
      .index_allocation = m_index_buffer_pool.allocate(index_allocation_size),
      .num_vertices = desc.num_vertices,
      .num_indices = desc.num_indices,
      .index_format = IndexFormat::U32,
  });

  unsigned offset = 0;

  auto positions = std::span(
      reinterpret_cast<const glm::vec3 *>(desc.positions), desc.num_vertices);
  mesh.positions_offset = offset;
  m_resource_uploader.stage_data(positions, mesh.vertex_allocation);
  offset += size_bytes(positions);

  if (desc.colors) {
    auto colors = std::span(reinterpret_cast<const glm::vec3 *>(desc.colors),
                            desc.num_vertices) |
                  map(encode_color);
    mesh.colors_offset = offset;
    m_resource_uploader.stage_data(colors, mesh.vertex_allocation,
                                   mesh.colors_offset);
    offset += size_bytes(colors);
  }

  auto indices = std::span(desc.indices, desc.num_indices);
  m_resource_uploader.stage_data(indices, mesh.index_allocation);

  return get_mesh_id(key);
}

void Scene::destroy_mesh(ren::MeshID id) {
  auto key = get_mesh_key(id);
  auto it = m_meshes.find(key);
  assert(it != m_meshes.end() and "Unknown mesh");
  auto &mesh = it->second;
  m_vertex_buffer_pool.free(mesh.vertex_allocation);
  m_index_buffer_pool.free(mesh.index_allocation);
  m_meshes.erase(key);
}

MaterialID Scene::create_material(const MaterialDesc &desc) {
  auto &&[key, material] = m_materials.emplace(Material{
      .pipeline = m_device->getPipelineCompiler().get_material_pipeline({
          .rt_format = m_rt_format,
          .albedo = static_cast<MaterialAlbedo>(desc.albedo_type),
      }),
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

void Scene::set_camera(const CameraDesc &desc) {
  auto pos = glm::make_vec3(desc.position);
  auto fwd = glm::make_vec3(desc.forward);
  auto up = glm::make_vec3(desc.up);
  m_camera.view = glm::lookAt(pos, pos + fwd, up);

  float ar = float(m_output_width) / float(m_output_height);
  switch (desc.type) {
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

auto Scene::create_model(const ModelDesc &desc) -> ModelID {
  return get_model_id(m_models.insert({
      .mesh = desc.mesh,
      .material = desc.material,
  }));
}

void Scene::destroy_model(ModelID model) {
  m_models.erase(get_model_key(model));
}

auto Scene::get_mesh(ren::MeshID mesh) const -> const ren::Mesh & {
  auto key = get_mesh_key(mesh);
  assert(m_meshes.contains(key) && "Unknown mesh");
  return m_meshes[key];
}

auto Scene::get_mesh(ren::MeshID mesh) -> ren::Mesh & {
  auto key = get_mesh_key(mesh);
  assert(m_meshes.contains(key) && "Unknown mesh");
  return m_meshes[key];
}

auto Scene::get_material(ren::MaterialID material) const
    -> const ren::Material & {
  auto key = get_material_key(material);
  assert(m_materials.contains(key) && "Unknown material");
  return m_materials[key];
}

auto Scene::get_material(ren::MaterialID material) -> ren::Material & {
  auto key = get_material_key(material);
  assert(m_materials.contains(key) && "Unknown material");
  return m_materials[key];
}

auto Scene::get_model(ModelID model) const -> const Model & {
  auto key = get_model_key(model);
  assert(m_models.contains(key) && "Unknown model");
  return m_models[key];
}

auto Scene::get_model(ModelID model) -> Model & {
  auto key = get_model_key(model);
  assert(m_models.contains(key) && "Unknown model");
  return m_models[key];
}

void Scene::set_model_matrix(ModelID model, const glm::mat4 &matrix) {
  get_model(model).matrix = matrix;
}

void Scene::begin_frame() {
  m_device->begin_frame();
  m_resource_uploader.begin_frame();
}

void Scene::end_frame() {
  m_resource_uploader.end_frame();
  m_device->end_frame();
}

void Scene::draw() {
  m_resource_uploader.upload_data();

  auto rgb = m_device->createRenderGraphBuilder();

  // Draw scene
  auto draw = rgb->addNode();
  draw.setDesc("Color pass");

  auto rt = draw.addOutput(
      RGTextureDesc{
          .format = m_rt_format,
          .width = m_output_width,
          .height = m_output_height,
      },
      MemoryAccess::ColorWrite, PipelineStage::ColorOutput);
  rgb->setDesc(rt, "Color buffer");

  auto global_cbuffer = draw.add_output(
      RGBufferDesc{
          .location = BufferLocation::Host,
          .size = unsigned(sizeof(GlobalData)),
      },
      MemoryAccess::UniformRead,
      PipelineStage::VertexShader | PipelineStage::FragmentShader);
  rgb->set_desc(global_cbuffer, "Global cbuffer");

  auto matrix_buffer = draw.add_output(
      RGBufferDesc{
          .location = BufferLocation::Host,
          .size = unsigned(sizeof(glm::mat3x4) * m_models.size()),
      },
      MemoryAccess::StorageRead, PipelineStage::VertexShader);
  rgb->set_desc(matrix_buffer, "Model matrix buffer");

  draw.setCallback([this, rt, matrix_buffer, global_cbuffer](CommandBuffer &cmd,
                                                             RenderGraph &rg) {
    const auto &signature = m_device->getPipelineCompiler().get_signature();

    cmd.beginRendering(rg.getTexture(rt));
    cmd.set_viewport({
        .width = float(m_output_width),
        .height = float(m_output_height),
    });
    cmd.set_scissor_rect({.width = m_output_width, .height = m_output_height});

    const auto &global_cb = rg.get_buffer(global_cbuffer);
    *global_cb.map<GlobalData>() = {.proj_view = m_camera.proj * m_camera.view};

    auto *matrices = rg.get_buffer(matrix_buffer).map<glm::mat3x4>();

    // TODO: Bind global cbuffer
    // TODO: Bind matrices
    // TODO: Bind materials

    for (const auto &&[i, model] :
         ranges::views::enumerate(m_models.values())) {
      matrices[i] = model.matrix;

      const auto &mesh = get_mesh(model.mesh);
      const auto &material = get_material(model.material);

      cmd.bind_graphics_pipeline(material.pipeline);

      auto addr = m_device->get_buffer_device_address(mesh.vertex_allocation);
      ModelData data = {
          .matrix_index = unsigned(i),
          .material_index = material.index,
          .positions = addr + mesh.positions_offset,
          .colors = (mesh.colors_offset != ATTRIBUTE_UNUSED)
                        ? addr + mesh.colors_offset
                        : 0,
      };
      cmd.set_graphics_push_constants(
          signature, ShaderStage::Vertex | ShaderStage::Fragment, data);

      cmd.bind_index_buffer(mesh.index_allocation, mesh.index_format);
      cmd.draw_indexed(mesh.num_indices);
    }

    cmd.endRendering();
  });

  // Post-process
  auto pp = rgb->addNode();
  pp.setDesc("Post-process pass");
  auto pprt = pp.addWriteInput(
      rt, MemoryAccess::StorageRead | MemoryAccess::StorageWrite,
      PipelineStage::ComputeShader);
  rgb->setDesc(pprt, "Post-processed color buffer");
  pp.setCallback([](CommandBuffer &cmd, RenderGraph &rg) {});

  // Present to swapchain
  rgb->setSwapchain(m_swapchain);
  rgb->setFinalImage(pprt);

  auto rg = rgb->build();

  rg->execute();

  end_frame();
  begin_frame();
}
