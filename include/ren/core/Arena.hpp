#pragma once
#include "Assert.hpp"
#include "NotNull.hpp"
#include "StdDef.hpp"
#include "Vm.hpp"

#include <initializer_list>

namespace ren {

constexpr usize ARENA_PAGE_SIZE = 4 * KiB;
constexpr usize ARENA_DEFAULT_SIZE = 256 * MiB;

struct Arena {
  void *m_ptr = nullptr;
  usize m_max_size = 0;
  usize m_committed_size = 0;
  usize m_offset = 0;

public:
  [[nodiscard]] static Arena init();

  void destroy(this Arena self);

  void clear() { m_offset = 0; }

  void commit(usize size) {
    ren_assert(size <= m_max_size);
    size = (size + ARENA_PAGE_SIZE - 1) & ~(ARENA_PAGE_SIZE - 1);
    if (size <= m_committed_size) {
      return;
    }
    vm_commit((u8 *)m_ptr + m_committed_size, size - m_committed_size);
    m_committed_size = size;
  }

  void *allocate(usize size, usize alignment) {
    usize aligned_offset = (m_offset + alignment - 1) & ~(alignment - 1);
    usize new_arena_size = aligned_offset + size;
    ren_assert(new_arena_size <= m_max_size);
    commit(new_arena_size);
    void *ptr = (u8 *)m_ptr + aligned_offset;
    m_offset = new_arena_size;
    return ptr;
  }

  template <typename T>
    requires std::is_trivially_destructible_v<T>
  auto allocate(usize count = 1) -> T * {
    void *ptr = allocate(count * sizeof(T), alignof(T));
    if (not std::is_trivially_constructible_v<T>) {
      return new (ptr) T[count];
    }
    return (T *)ptr;
  }

  void *expand(void *ptr, usize old_size, usize new_size);

  template <typename T> T *expand(T *ptr, usize old_count, usize new_count) {
    return (T *)expand((void *)ptr, old_count * sizeof(T),
                       new_count * sizeof(T));
  }
};

inline Arena make_arena() { return Arena::init(); }

inline void destroy(Arena arena) { arena.destroy(); }

inline void clear(NotNull<Arena *> arena) { arena->clear(); }

inline void commit(NotNull<Arena *> arena, usize size) { arena->commit(size); }

inline void *allocate(NotNull<Arena *> arena, usize size, usize alignment) {
  return arena->allocate(size, alignment);
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

inline void *expand(NotNull<Arena *> arena, void *ptr, usize old_size,
                    usize new_size) {
  return arena->expand(ptr, old_size, new_size);
}

template <typename T>
T *expand(NotNull<Arena *> arena, T *ptr, usize old_count, usize new_count) {
  return (T *)expand(arena, (void *)ptr, old_count * sizeof(T),
                     new_count * sizeof(T));
}

constexpr usize MAX_SCRATCH_ARENAS = 4;

struct ScratchArena {
  Arena *m_arena = nullptr;
  usize m_offset = 0;

public:
  static Arena *pool;

  static void init_allocator();
  static void *get_allocator();
  static void set_allocator(void *allocator);

  ScratchArena() : ScratchArena({}) {}
  ScratchArena(Arena *conflict) : ScratchArena({conflict}) {}

  ScratchArena(std::initializer_list<Arena *> conflicts) {
    for (usize i = 0; i < MAX_SCRATCH_ARENAS; ++i) {
      bool has_conflict = false;
      for (Arena *conflict : conflicts) {
        if (conflict->m_ptr == pool[i].m_ptr) {
          has_conflict = true;
          break;
        }
      }
      if (!has_conflict) {
        m_arena = &pool[i];
        m_offset = m_arena->m_offset;
        break;
      }
    }
    ren_assert(m_arena);
  }

  ~ScratchArena() { m_arena->m_offset = m_offset; }

  Arena *operator->() & { return m_arena; }
  operator Arena *() & { return m_arena; }
  operator NotNull<Arena *>() & { return m_arena; }
};

} // namespace ren
