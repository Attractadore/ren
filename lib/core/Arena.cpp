#include "ren/core/Arena.hpp"
#include "ren/core/Vm.hpp"

namespace ren {

auto make_arena() -> Arena {
  Arena arena = {.max_size = ARENA_DEFAULT_SIZE};
  while (arena.max_size > 0) {
    arena.ptr = vm_allocate(arena.max_size);
    if (arena.ptr) {
      break;
    }
    arena.max_size /= 2;
  }
  return arena;
}

void destroy(Arena arena) { vm_free(arena.ptr, arena.max_size); }

void commit(NotNull<Arena *> arena, usize size) {
  size = (size + ARENA_PAGE_SIZE - 1) & ~(ARENA_PAGE_SIZE - 1);
  size = std::min(size, arena->max_size);
  size = std::max(size, arena->committed_size);
  size = std::max(size, ARENA_PAGE_SIZE);
  vm_commit((u8 *)arena->ptr + arena->committed_size,
            size - arena->committed_size);
  arena->committed_size = size;
}

} // namespace ren
