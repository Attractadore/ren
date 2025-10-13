#pragma once
#include "Assert.hpp"
#include "NotNull.hpp"
#include "StdDef.hpp"
#include "Vm.hpp"

namespace ren {

constexpr usize ARENA_PAGE_SIZE = 4 * KiB;
constexpr usize ARENA_DEFAULT_SIZE = 256 * MiB;

struct Arena {
  void *ptr = nullptr;
  usize max_size = 0;
  usize committed_size = 0;
  usize offset = 0;
};

auto make_arena() -> Arena;

void destroy(Arena arena);

inline void clear(NotNull<Arena *> arena) { arena->offset = 0; }

inline void commit(NotNull<Arena *> arena, usize size) {
  ren_assert(size <= arena->max_size);
  size = (size + ARENA_PAGE_SIZE - 1) & ~(ARENA_PAGE_SIZE - 1);
  if (size <= arena->committed_size) {
    return;
  }
  vm_commit((u8 *)arena->ptr + arena->committed_size,
            size - arena->committed_size);
  arena->committed_size = size;
}

inline auto allocate(NotNull<Arena *> arena, usize size, usize alignment)
    -> void * {
  usize aligned_offset = (arena->offset + alignment - 1) & ~(alignment - 1);
  usize new_arena_size = aligned_offset + size;
  ren_assert(new_arena_size <= arena->max_size);
  commit(arena, new_arena_size);
  void *ptr = (u8 *)arena->ptr + aligned_offset;
  arena->offset = new_arena_size;
  return ptr;
}

template <typename T>
  requires std::is_trivially_destructible_v<T>
auto allocate(NotNull<Arena *> arena, usize count = 1) -> T * {
  void *ptr = allocate(arena, count * sizeof(T), alignof(T));
  if (not std::is_trivially_constructible_v<T>) {
    return new (ptr) T[count];
  }
  return (T *)ptr;
}

void *expand(NotNull<Arena *> arena, void *ptr, usize old_size, usize new_size);

template <typename T>
T *expand(NotNull<Arena *> arena, T *ptr, usize old_count, usize new_count) {
  return (T *)expand(arena, (void *)ptr, old_count * sizeof(T),
                     new_count * sizeof(T));
}

} // namespace ren
