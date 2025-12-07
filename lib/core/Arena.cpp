#include "ren/core/Arena.hpp"

#include "ren/core/Algorithm.hpp"
#include "ren/core/BlockAllocator.hpp"
#include "ren/core/Math.hpp"
#include "ren/core/Vm.hpp"

namespace ren {

thread_local BlockAllocator thread_allocator;

ArenaBlock *job_allocate_block(usize size);
void job_free_block(ArenaBlock *block);
bool job_use_global_allocator();

void *job_tag_allocate(ArenaTag tag, usize size, usize alignment);

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

Arena Arena::from_tag(ArenaTag tag) {
  ren_assert(tag.m_id != 0);
  Arena arena = {
      .m_tag = tag,
      .m_type = ArenaType::Tagged,
  };
  return arena;
}

void Arena::destroy() {
  switch (m_type) {
  case ArenaType::Dedicated:
    vm_free(m_ptr, m_allocation_size);
    break;
  case ArenaType::Tagged:
    break;
  case ArenaType::ThreadScratch: {
    auto *head = m_head;
    while (head) {
      ArenaBlock *next = head->next;
      free_block(&thread_allocator, head, head->block_size);
      head = next;
    }
  } break;
  case ArenaType::JobScratch: {
    auto *head = m_head;
    while (head) {
      ArenaBlock *next = head->next;
      job_free_block(head);
      head = next;
    }
  } break;
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
  if (m_type == ArenaType::Dedicated) {
    usize aligned_offset = (m_offset + alignment - 1) & ~(alignment - 1);
    usize new_offset = aligned_offset + size;
    [[unlikely]] if (new_offset > m_size) { vm_arena_commit(this, new_offset); }
    m_offset = new_offset;
    return (u8 *)m_ptr + aligned_offset;
  }

  usize aligned_offset =
      (sizeof(ArenaBlock) + alignment - 1) & ~(alignment - 1);
  usize block_size = 0;
  ArenaBlock *head = nullptr;
  if (m_type == ArenaType::ThreadScratch) {
    block_size =
        max(THREAD_ALLOCATOR_BLOCK_SIZE, next_po2(aligned_offset + size));
    head = (ArenaBlock *)allocate_block(&thread_allocator, block_size);
    head->block_size = block_size;
    head->block_offset = 0;
  } else if (m_type == ArenaType::JobScratch) {
    block_size = max(JOB_ALLOCATOR_BLOCK_SIZE, next_po2(aligned_offset + size));
    head = job_allocate_block(block_size);
  } else {
    ren_assert(m_type == ArenaType::Tagged);
    // TODO: pick a different block size when the tagged allocator is
    // implemented.
    block_size = max(4 * KiB, next_po2(aligned_offset + size));
    head =
        (ArenaBlock *)job_tag_allocate(m_tag, block_size, alignof(max_align_t));
    head->block_size = block_size;
    head->block_offset = 0;
  }
  head->next = m_head;
  m_head = head;
  m_size = block_size;
  m_offset = aligned_offset + size;

  return (u8 *)head + aligned_offset;
}

void *Arena::expand(void *ptr, usize old_size, usize new_size) {
  if (m_type == ArenaType::Dedicated) {
    ren_assert(ptr >= m_ptr and ptr <= (u8 *)m_ptr + m_allocation_size);
    usize offset = (u8 *)ptr - (u8 *)m_ptr;
    if (offset + old_size != m_offset) {
      return nullptr;
    }
    [[unlikely]] if (offset + new_size > m_size) {
      vm_arena_commit(this, offset + new_size);
    }
    m_offset = offset + new_size;
    return ptr;
  }

  if ((u8 *)ptr + old_size != (u8 *)m_ptr + m_offset) {
    return nullptr;
  }
  usize offset = (u8 *)ptr - (u8 *)m_ptr;
  if (offset + new_size > m_size) {
    return nullptr;
  }
  m_offset = offset + new_size;

  return ptr;
}

void ScratchArena::init_for_thread() {
  init_allocator(&thread_allocator, THREAD_ALLOCATOR_BLOCK_SIZE);
}

void ScratchArena::destroy_for_thread() {
  destroy_allocator(&thread_allocator);
}

ScratchArena::ScratchArena() {
  m_arena = {
      .m_type = job_use_global_allocator() ? ArenaType::JobScratch
                                           : ArenaType::ThreadScratch,
  };
}

} // namespace ren
