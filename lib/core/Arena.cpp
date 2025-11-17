#include "ren/core/Arena.hpp"
#include "ren/core/Algorithm.hpp"
#include "ren/core/Math.hpp"
#include "ren/core/Vm.hpp"

#include <fmt/base.h>
#include <tracy/Tracy.hpp>
#include <utility>

namespace ren {

struct ThreadAllocator {
  // Mask of free masks.
  u64 free_masks = 0;
  // Masks of free blocks.
  u64 free_blocks[32] = {};
  void *pool = nullptr;
  usize pool_size = 0;
  usize commit_size = 0;
};

thread_local ThreadAllocator thread_allocator;

void ScratchArena::init_for_thread() {
  thread_allocator.pool_size = MAX_THREAD_ALLOCATOR_SIZE;

retry:
  thread_allocator.pool = vm_allocate(thread_allocator.pool_size);
  if (!thread_allocator.pool) {
    thread_allocator.pool_size /= 2;
    goto retry;
  }

  u64 num_blocks = thread_allocator.pool_size / MIN_THREAD_ARENA_BLOCK_SIZE;
  ren_assert(num_blocks > 0);
  u64 num_masks = (num_blocks + 63) / 64;
  thread_allocator.free_masks = (u64(1) << num_masks) - 1;
  for (usize i : range(num_blocks / 64)) {
    thread_allocator.free_blocks[i] = -1;
  }
  if (num_blocks % 64 != 0) {
    thread_allocator.free_blocks[num_blocks / 64] =
        (1 << (num_blocks % 64)) - 1;
  }
}

void ScratchArena::destroy_for_thread() {
  vm_free(thread_allocator.pool, thread_allocator.pool_size);
  thread_allocator = {};
}

template <usize N> static usize thread_find_first_block() {
  ZoneScoped;
  if constexpr (N < 64) {
    u64 free_masks = thread_allocator.free_masks;
    while (free_masks) {
      u64 mask_index = find_lsb(free_masks);
      usize first_block =
          find_aligned_ones<N>(thread_allocator.free_blocks[mask_index]);
      if (first_block != (u64)-1) {
        u64 block_mask = (u64(1) << N) - 1;
        block_mask = block_mask << first_block;
        ren_assert((thread_allocator.free_blocks[mask_index] & block_mask) ==
                   block_mask);
        thread_allocator.free_blocks[mask_index] &= ~block_mask;
        bool mask_empty = thread_allocator.free_blocks[mask_index] == 0;
        thread_allocator.free_masks &= ~(u64(mask_empty) << mask_index);
        return mask_index * 64 + first_block;
      }
      free_masks = free_masks & ~(u64(1) << mask_index);
    }
  } else {
    usize num_masks = N / 64;
    for (usize first_mask = 0; first_mask < 32; first_mask += num_masks) {
      bool all_free = true;
      for (usize j : range<usize>(0, num_masks)) {
        all_free = thread_allocator.free_blocks[first_mask + j] == (u64)-1 and
                   all_free;
      }
      if (all_free) {
        for (usize j : range<usize>(0, num_masks)) {
          ren_assert(thread_allocator.free_blocks[first_mask + j] == (u64)-1);
          thread_allocator.free_blocks[first_mask + j] = 0;
        }
        u64 masks_mask = (u64(1) << num_masks) - 1;
        thread_allocator.free_masks &= ~(masks_mask << first_mask);
        return first_mask * 64;
      }
    }
  }
  fmt::println(stderr, "Thread allocator overflow");
  ren_trap();
  std::abort();
}

static void *thread_allocate_block_slow(usize size) {
  ZoneScoped;
  usize num_blocks = size / MIN_THREAD_ARENA_BLOCK_SIZE;
  usize first_block = [&]() {
    switch (num_blocks) {
    case 2:
      return thread_find_first_block<2>();
    case 4:
      return thread_find_first_block<4>();
    case 8:
      return thread_find_first_block<8>();
    case 16:
      return thread_find_first_block<16>();
    case 32:
      return thread_find_first_block<32>();
    case 64:
      return thread_find_first_block<64>();
    case 2 * 64:
      return thread_find_first_block<2 * 64>();
    case 4 * 64:
      return thread_find_first_block<4 * 64>();
    case 8 * 64:
      return thread_find_first_block<8 * 64>();
    case 16 * 64:
      return thread_find_first_block<16 * 64>();
    case 32 * 64:
      return thread_find_first_block<32 * 64>();
    }
    std::unreachable();
  }();
  usize offset = first_block * MIN_THREAD_ARENA_BLOCK_SIZE;
  [[unlikely]] if (offset + size > thread_allocator.commit_size) {
    vm_commit((u8 *)thread_allocator.pool + thread_allocator.commit_size,
              offset + size - thread_allocator.commit_size);
    thread_allocator.commit_size = offset + size;
  }
  return (u8 *)thread_allocator.pool + offset;
}

static void *thread_allocate_block(usize size) {
  ZoneScoped;
  [[likely]] if (size == MIN_THREAD_ARENA_BLOCK_SIZE) {
    usize free_mask = find_lsb(thread_allocator.free_masks);
    [[unlikely]] if (free_mask == (u64)-1) {
      fmt::println(stderr, "Thread allocator overflow");
      ren_trap();
      std::abort();
    }
    usize free_block = find_lsb(thread_allocator.free_blocks[free_mask]);
    ren_assert(free_block != (u64)-1);
    ren_assert(thread_allocator.free_blocks[free_mask] &
               (u64(1) << free_block));
    thread_allocator.free_blocks[free_mask] &= ~(u64(1) << free_block);
    bool mask_empty = thread_allocator.free_blocks[free_mask] == 0;
    thread_allocator.free_masks &= ~(u64(mask_empty) << free_mask);
    usize offset = (free_mask * 64 + free_block) * MIN_THREAD_ARENA_BLOCK_SIZE;
    [[unlikely]] if ((offset + size) > thread_allocator.commit_size) {
      vm_commit((u8 *)thread_allocator.pool + thread_allocator.commit_size,
                offset + size - thread_allocator.commit_size);
      thread_allocator.commit_size = offset + size;
    }
    return (u8 *)thread_allocator.pool + offset;
  }
  return thread_allocate_block_slow(size);
}

static void thread_free_block(void *ptr, usize size) {
  usize offset = (u8 *)ptr - (u8 *)thread_allocator.pool;
  ren_assert(offset + size < thread_allocator.pool_size);
  ren_assert((offset & (size - 1)) == 0);
  usize first_block = offset / MIN_THREAD_ARENA_BLOCK_SIZE;
  usize num_blocks = size / MIN_THREAD_ARENA_BLOCK_SIZE;
  [[likely]] if (num_blocks <= 32) {
    usize mask_index = first_block / 64;
    first_block = first_block % 64;
    usize block_mask = (u64(1) << num_blocks) - 1;
    block_mask = block_mask << first_block;
    ren_assert((thread_allocator.free_blocks[mask_index] & block_mask) == 0);
    thread_allocator.free_blocks[mask_index] |= block_mask;
    thread_allocator.free_masks |= u64(1) << mask_index;
  } else {
    usize first_mask = first_block / 64;
    usize num_masks = num_blocks / 64;
    for (usize mask_index : range(num_masks)) {
      ren_assert(thread_allocator.free_blocks[first_mask + mask_index] == 0);
      thread_allocator.free_blocks[first_mask + mask_index] = -1;
    }
    u64 masks_mask = (u64(1) << num_masks) - 1;
    thread_allocator.free_masks |= masks_mask << first_mask;
  }
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
      thread_free_block(head, head->size);
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
    ArenaBlock *head = (ArenaBlock *)thread_allocate_block(block_size);
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
