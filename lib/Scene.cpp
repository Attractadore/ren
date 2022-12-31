#include "Scene.hpp"
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
  });

  auto positions = std::span(
      reinterpret_cast<const glm::vec3 *>(desc.positions), desc.num_vertices);
  m_resource_uploader.stage_data(positions, mesh.vertex_allocation);
  unsigned offset = size_bytes(positions);

  if (desc.colors) {
    auto colors = std::span(reinterpret_cast<const glm::vec3 *>(desc.colors),
                            desc.num_vertices) |
                  map(encode_color);
    m_resource_uploader.stage_data(colors, mesh.vertex_allocation,
                                   mesh.colors_offset);

    mesh.colors_offset = offset;
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
  RGTextureDesc rt_desc = {
      .format = Format::RGBA16F,
      .width = m_output_width,
      .height = m_output_height,
  };
  auto rt = draw.addOutput(rt_desc, MemoryAccess::ColorWrite,
                           PipelineStage::ColorOutput);
  rgb->setDesc(rt, "Color buffer");
  draw.setCallback([=](CommandBuffer &cmd, RenderGraph &rg) {
    cmd.beginRendering(rg.getTexture(rt));
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
