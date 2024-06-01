#include "Mesh.hpp"
#include "ResourceArena.hpp"

namespace ren {

auto create_index_pool(ResourceArena &arena) -> IndexPool {
  IndexPool pool;

  pool.indices = arena
                     .create_buffer({
                         .name = "Mesh vertex indices pool",
                         .heap = BufferHeap::Static,
                         .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                         .size = sizeof(u32) * pool.num_free_indices,
                     })
                     .buffer;

  return pool;
}

} // namespace ren
