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

void destroy(NotNull<Arena *> arena) {
  vm_free(arena->ptr, arena->max_size);
  *arena = {};
}

} // namespace ren
