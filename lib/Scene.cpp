#include "Scene.hpp"
#include "CommandBuffer.hpp"
#include "Device.hpp"
#include "RenderGraph.hpp"
#include "Support/Array.hpp"
#include "hlsl/encode.hlsl"

#include <range/v3/algorithm.hpp>
#include <range/v3/range.hpp>
#include <range/v3/view.hpp>

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
          m_device, {
                        .usage = BufferUsage::TransferDST | BufferUsage::Index,
                        .location = BufferLocation::Device,
                        .size = 1 << 22,
                    }) {}

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

  unsigned offset = sizeof(glm::vec3) * mesh.num_vertices;
  if (desc.colors) {
    mesh.colors_offset = offset;
    offset += sizeof(color_t) * mesh.num_vertices;
  }

  if (mesh.vertex_allocation.desc.ptr) {
    auto *positions = get_host_ptr<glm::vec3>(mesh.vertex_allocation);
    ranges::copy_n(reinterpret_cast<const glm::vec3 *>(desc.positions),
                   mesh.num_vertices, positions);
    if (desc.colors) {
      auto *colors =
          get_host_ptr<color_t>(mesh.vertex_allocation, mesh.colors_offset);
      ranges::copy(
          ranges::views::transform(
              std::span(reinterpret_cast<const glm::vec3 *>(desc.colors),
                        mesh.num_vertices),
              encode_color),
          colors);
    }
  } else {
    assert(!"FIXME: Buffers must be host visible");
  }

  if (mesh.index_allocation.desc.ptr) {
    auto *indices = get_host_ptr<unsigned>(mesh.index_allocation);
    ranges::copy_n(desc.indices, mesh.num_indices, indices);
  } else {
    assert(!"FIXME: Buffers must be host visible");
  }

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

void Scene::draw() {
  m_device->begin_frame();
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

  m_device->end_frame();
}
