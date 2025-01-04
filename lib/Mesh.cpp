#include "Mesh.hpp"
#include "ResourceArena.hpp"

namespace ren {

auto create_index_pool(ResourceArena &arena) -> IndexPool {
  IndexPool pool;

  pool.indices = arena
                     .create_buffer({
                         .name = "Mesh vertex indices pool",
                         .heap = rhi::MemoryHeap::Default,
                         .size = sizeof(u8) * pool.num_free_indices,
                     })
                     .value()
                     .buffer;

  return pool;
}

} // namespace ren
