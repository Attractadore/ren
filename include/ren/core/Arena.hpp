#pragma once
#include "NotNull.hpp"
#include "StdDef.hpp"

#include <new>

namespace ren {

enum class ArenaNamedTag {
  None,
  EditorProject,
  EditorCompile,
  EditorImportScene,
  FirstCustom,
};

struct ArenaTag {
public:
  ArenaTag() = default;
  ArenaTag(ArenaNamedTag name) { m_name = name; }
  ArenaTag(u64 id) { m_id = id; }

  union {
    ArenaNamedTag m_name;
    u64 m_id = 0;
  };
};

enum class ArenaType {
  Dedicated,
  Tagged,
  ThreadScratch,
  JobScratch,
};

struct ArenaBlock {
  ArenaBlock *next;
  u32 block_size;
  u32 block_offset;
};

constexpr usize MAX_DEDICATED_ARENA_SIZE = 4 * GiB;

constexpr usize THREAD_ALLOCATOR_BLOCK_SIZE = 2 * MiB;

constexpr usize JOB_ALLOCATOR_BIG_BLOCK_SIZE = 2 * MiB;
constexpr usize JOB_ALLOCATOR_BLOCK_SIZE = JOB_ALLOCATOR_BIG_BLOCK_SIZE / 64;

struct Arena {
  union {
    void *m_ptr = nullptr;
    ArenaBlock *m_head;
  };
  union {
    struct {
      usize m_page_size;
      usize m_allocation_size;
    };
    ArenaTag m_tag = ArenaNamedTag::None;
  };
  usize m_size = 0;
  usize m_offset = 0;
  ArenaType m_type = {};

public:
  [[nodiscard]] static Arena init();
  [[nodiscard]] static Arena from_tag(ArenaTag tag);

  void destroy();

  void clear() { m_offset = 0; }

  ALWAYS_INLINE void *allocate(usize size, usize alignment) {
    usize aligned_offset = (m_offset + alignment - 1) & ~(alignment - 1);
    usize new_offset = aligned_offset + size;
    void *ptr = (u8 *)m_ptr + aligned_offset;
    [[unlikely]] if (new_offset > m_size) {
      return allocate_slow(size, alignment);
    }
    m_offset = new_offset;
    return ptr;
  }

  template <typename T>
    requires std::is_trivially_destructible_v<T> and
             std::is_default_constructible_v<T>
  ALWAYS_INLINE auto allocate(usize count = 1) -> T * {
    void *ptr = allocate(count * sizeof(T), alignof(T));
    if constexpr (not std::is_trivially_default_constructible_v<T>) {
      new (ptr) T[count];
    }
    return (T *)ptr;
  }

  void *expand(void *ptr, usize old_size, usize new_size);

  template <typename T> T *expand(T *ptr, usize old_count, usize new_count) {
    return (T *)expand((void *)ptr, old_count * sizeof(T),
                       new_count * sizeof(T));
  }

  explicit operator bool() const { return m_ptr; }

private:
  void *allocate_slow(usize size, usize alignment);
};

struct ScratchArena {
  Arena m_arena;

public:
  static void init_for_thread();
  static void destroy_for_thread();

  ScratchArena();
  ~ScratchArena() { m_arena.destroy(); }

  Arena *operator->() & { return &m_arena; }
  operator Arena *() & { return &m_arena; }
  operator NotNull<Arena *>() & { return &m_arena; }
};

} // namespace ren
