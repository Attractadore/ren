#include "ren/core/Arena.hpp"

#include "ren/core/Algorithm.hpp"
#include "ren/core/BlockAllocator.hpp"
#include "ren/core/Math.hpp"
#include "ren/core/Vm.hpp"

#include <utility>

namespace ren {

thread_local BlockAllocator thread_allocator;

void ScratchArena::init_for_thread() {
  init_allocator(&thread_allocator, MIN_THREAD_ARENA_BLOCK_SIZE);
}

void ScratchArena::destroy_for_thread() {
  destroy_allocator(&thread_allocator);
}

Arena Arena::init() {
  Arena arena = {
      .m_page_size = vm_page_size(),
      .m_allocation_size = MAX_DEDICATED_ARENA_SIZE,
      .m_type = ArenaType::Dedicated,
  };
  while (arena.m_allocation_size > 0) {
    arena.m_ptr = vm_allocate(arena.m_allocation_size);
    if (arena.m_ptr) {
      break;
    }
    arena.m_allocation_size /= 2;
  }
  ren_assert(arena.m_allocation_size > 0);
  return arena;
}

void Arena::destroy() {
  switch (m_type) {
  case ArenaType::Dedicated:
    vm_free(m_ptr, m_allocation_size);
    break;
  case ArenaType::ThreadScratch: {
    auto *head = m_head;
    while (head) {
      ArenaBlock *next = head->next;
      free_block(&thread_allocator, head, head->size);
      head = next;
    }
  } break;
  case ArenaType::JobScratch:
    std::abort();
  }
}

static void vm_arena_commit(NotNull<Arena *> arena, usize new_commit_size) {
  ren_assert(arena->m_type == ArenaType::Dedicated);
  new_commit_size =
      (new_commit_size + arena->m_page_size - 1) & ~(arena->m_page_size - 1);
  ren_assert(new_commit_size > arena->m_size);
  vm_commit((u8 *)arena->m_ptr + arena->m_size,
            new_commit_size - arena->m_size);
  arena->m_size = new_commit_size;
}

void *Arena::allocate_slow(usize size, usize alignment) {
  switch (m_type) {
  case ArenaType::Dedicated: {
    usize aligned_offset = (m_offset + alignment - 1) & ~(alignment - 1);
    usize new_offset = aligned_offset + size;
    [[unlikely]] if (new_offset > m_size) { vm_arena_commit(this, new_offset); }
    m_offset = new_offset;
    return (u8 *)m_ptr + aligned_offset;
  }
  case ArenaType::ThreadScratch: {
    usize aligned_offset =
        (sizeof(ArenaBlock) + alignment - 1) & ~(alignment - 1);
    usize block_size =
        max(MIN_THREAD_ARENA_BLOCK_SIZE, next_po2(aligned_offset + size));
    ArenaBlock *head =
        (ArenaBlock *)allocate_block(&thread_allocator, block_size);
    head->next = m_head;
    head->size = block_size;
    m_head = head;
    m_size = block_size;
    m_offset = aligned_offset + size;
    return (u8 *)head + aligned_offset;
  }
  case ArenaType::JobScratch:
    std::abort();
  }
  std::unreachable();
}

void *Arena::expand(void *ptr, usize old_size, usize new_size) {
  usize offset = (u8 *)ptr - (u8 *)m_ptr;
  if (offset + old_size == m_offset) {
    switch (m_type) {
    case ArenaType::Dedicated: {
      ren_assert(ptr >= m_ptr and ptr <= (u8 *)m_ptr + m_allocation_size);
      [[unlikely]] if (offset + new_size > m_size) {
        vm_arena_commit(this, offset + new_size);
      }
      m_offset = offset + new_size;
    } break;
    case ArenaType::ThreadScratch: {
      if (offset + new_size > MIN_THREAD_ARENA_BLOCK_SIZE) {
        return nullptr;
      }
      m_offset = offset + new_size;
    } break;
    case ArenaType::JobScratch:
      std::abort();
    }
    return ptr;
  }
  return nullptr;
}

bool job_use_global_allocator();

ScratchArena::ScratchArena() {
  if (job_use_global_allocator()) {
    std::abort();
  }
  m_arena = {
      .m_size = MIN_THREAD_ARENA_BLOCK_SIZE,
      .m_offset = MIN_THREAD_ARENA_BLOCK_SIZE,
      .m_type = ArenaType::ThreadScratch,
  };
}

} // namespace ren
