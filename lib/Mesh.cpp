#include "Mesh.hpp"
#include "ResourceArena.hpp"

namespace ren {

auto create_vertex_pool(ResourceArena &arena, MeshAttributeFlags attributes)
    -> VertexPool {
  VertexPool pool;

  pool.positions =
      arena
          .create_buffer({
              .name = "Mesh vertex positions pool",
              .heap = BufferHeap::Static,
              .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
              .size = sizeof(glsl::Position) * glsl::NUM_VERTEX_POOL_VERTICES,
          })
          .buffer;

  pool.normals =
      arena
          .create_buffer({
              .name = "Mesh vertex normals pool",
              .heap = BufferHeap::Static,
              .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
              .size = sizeof(glsl::Normal) * glsl::NUM_VERTEX_POOL_VERTICES,
          })
          .buffer;

  if (attributes.isSet(MeshAttribute::Tangent)) {
    pool.tangents =
        arena
            .create_buffer({
                .name = "Mesh vertex tangents pool",
                .heap = BufferHeap::Static,
                .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                .size = sizeof(glsl::Tangent) * glsl::NUM_VERTEX_POOL_VERTICES,
            })
            .buffer;
  }

  if (attributes.isSet(MeshAttribute::UV)) {
    pool.uvs =
        arena
            .create_buffer({
                .name = "Mesh vertex UVs pool",
                .heap = BufferHeap::Static,
                .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                .size = sizeof(glsl::UV) * glsl::NUM_VERTEX_POOL_VERTICES,
            })
            .buffer;
  }

  if (attributes.isSet(MeshAttribute::Color)) {
    pool.colors =
        arena
            .create_buffer({
                .name = "Mesh vertex colors pool",
                .heap = BufferHeap::Static,
                .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                .size = sizeof(glsl::Color) * glsl::NUM_VERTEX_POOL_VERTICES,
            })
            .buffer;
  }

  pool.indices = arena
                     .create_buffer({
                         .name = "Mesh vertex indices pool",
                         .heap = BufferHeap::Static,
                         .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                         .size = sizeof(u32) * glsl::NUM_VERTEX_POOL_INDICES,
                     })
                     .buffer;

  return pool;
}

} // namespace ren
