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

void *expand(NotNull<Arena *> arena, void *ptr, usize old_size,
             usize new_size) {
  ren_assert(ptr >= arena->ptr and ptr <= (u8 *)arena->ptr + arena->max_size);
  usize offset = (u8 *)ptr - (u8 *)arena->ptr;
  if (offset + old_size == arena->offset) {
    commit(arena, offset + new_size);
    arena->offset = arena->offset + new_size;
    return ptr;
  }
  return nullptr;
}

} // namespace ren
