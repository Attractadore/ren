#include "Mesh.hpp"
#include "Renderer.hpp"

namespace ren {

auto create_vertex_pool(MeshAttributeFlags attributes) -> VertexPool {
  VertexPool pool;

  pool.positions = g_renderer->create_buffer({
      .name = "Mesh vertex positions pool",
      .heap = BufferHeap::Static,
      .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      .size = sizeof(glsl::Position) * glsl::NUM_VERTEX_POOL_VERTICES,
  });

  pool.normals = g_renderer->create_buffer({
      .name = "Mesh vertex normals pool",
      .heap = BufferHeap::Static,
      .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      .size = sizeof(glsl::Normal) * glsl::NUM_VERTEX_POOL_VERTICES,
  });

  if (attributes.isSet(MeshAttribute::Tangent)) {
    pool.tangents = g_renderer->create_buffer({
        .name = "Mesh vertex tangents pool",
        .heap = BufferHeap::Static,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .size = sizeof(glsl::Tangent) * glsl::NUM_VERTEX_POOL_VERTICES,
    });
  }

  if (attributes.isSet(MeshAttribute::UV)) {
    pool.uvs = g_renderer->create_buffer({
        .name = "Mesh vertex UVs pool",
        .heap = BufferHeap::Static,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .size = sizeof(glsl::UV) * glsl::NUM_VERTEX_POOL_VERTICES,
    });
  }

  if (attributes.isSet(MeshAttribute::Color)) {
    pool.colors = g_renderer->create_buffer({
        .name = "Mesh vertex colors pool",
        .heap = BufferHeap::Static,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .size = sizeof(glsl::Color) * glsl::NUM_VERTEX_POOL_VERTICES,
    });
  }

  pool.indices = g_renderer->create_buffer({
      .name = "Mesh vertex indices pool",
      .heap = BufferHeap::Static,
      .usage =
          VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
      .size = sizeof(u32) * glsl::NUM_VERTEX_POOL_INDICES,
  });

  return pool;
}

} // namespace ren
