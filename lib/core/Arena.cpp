#include "ren/core/Arena.hpp"
#include "ren/core/Vm.hpp"

namespace ren {

Arena Arena::init() {
  Arena arena = {.m_max_size = ARENA_DEFAULT_SIZE};
  while (arena.m_max_size > 0) {
    arena.m_ptr = vm_allocate(arena.m_max_size);
    if (arena.m_ptr) {
      break;
    }
    arena.m_max_size /= 2;
  }
  ren_assert(arena.m_max_size > 0);
  return arena;
}

void Arena::destroy(this Arena self) { vm_free(self.m_ptr, self.m_max_size); }

void *Arena::expand(void *ptr, usize old_size, usize new_size) {
  ren_assert(ptr >= m_ptr and ptr <= (u8 *)m_ptr + m_max_size);
  usize offset = (u8 *)ptr - (u8 *)m_ptr;
  if (offset + old_size == m_offset) {
    commit(offset + new_size);
    m_offset = m_offset + new_size;
    return ptr;
  }
  return nullptr;
}

static Arena scratch_arena_storage[MAX_SCRATCH_ARENAS];
Arena *ScratchArena::pool = nullptr;

void ScratchArena::init_allocator() {
  for (Arena &arena : scratch_arena_storage) {
    arena = Arena::init();
  }
  ScratchArena::pool = scratch_arena_storage;
}

void *ScratchArena::get_allocator() { return ScratchArena::pool; }

void ScratchArena::set_allocator(void *allocator) {
  ScratchArena::pool = (Arena *)allocator;
}

} // namespace ren
