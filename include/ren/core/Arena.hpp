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

void commit(NotNull<Arena *> arena, usize size);

inline void *aligned_ptr(const Arena &arena, usize alignment) {
  return (u8 *)arena.ptr + ((arena.offset + alignment - 1) & ~(alignment - 1));
}

template <typename T> inline T *aligned_ptr(const Arena &arena) {
  return (T *)aligned_ptr(arena, alignof(T));
}

inline void clear(NotNull<Arena *> arena) { arena->offset = 0; }

inline auto allocate(NotNull<Arena *> arena, usize size, usize alignment)
    -> void * {
  usize aligned_offset = (arena->offset + alignment - 1) & ~(alignment - 1);
  usize new_arena_size = aligned_offset + size;
  ren_assert(new_arena_size <= arena->max_size);

  [[unlikely]] if (new_arena_size >= arena->committed_size) {
    usize commit_size =
        std::max(new_arena_size - arena->committed_size, ARENA_PAGE_SIZE);
    vm_commit((u8 *)arena->ptr + arena->committed_size, commit_size);
    arena->committed_size += commit_size;
  }

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

} // namespace ren
