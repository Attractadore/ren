#include "ren/core/BlockAllocator.hpp"
#include "ren/core/Math.hpp"
#include "ren/core/Vm.hpp"

#include <fmt/base.h>
#include <tracy/Tracy.hpp>
#include <utility>

namespace ren {

void init_allocator(NotNull<BlockAllocator *> allocator, usize min_block_size) {
  *allocator = {
      .min_block_size = min_block_size,
      .pool_size = 64 * 64 * min_block_size,
  };

retry:
  allocator->pool = vm_allocate(allocator->pool_size);
  if (!allocator->pool) {
    allocator->pool_size /= 2;
    goto retry;
  }

  u64 num_blocks = allocator->pool_size / min_block_size;
  ren_assert(num_blocks > 0);
  for (usize i : range(num_blocks / 64)) {
    allocator->free_blocks[i] = -1;
    allocator->free_masks |= u64(1) << i;
  }
  if (num_blocks % 64 != 0) {
    allocator->free_blocks[num_blocks / 64] = (1 << (num_blocks % 64)) - 1;
    allocator->free_masks |= u64(1) << (num_blocks / 64);
  }
}

void destroy_allocator(NotNull<BlockAllocator *> allocator) {
  vm_free(allocator->pool, allocator->pool_size);
  *allocator = {};
}

template <usize N>
static usize find_first_block(NotNull<BlockAllocator *> allocator) {
  ZoneScoped;
  if constexpr (N < 64) {
    u64 free_masks = allocator->free_masks;
    while (free_masks) {
      u64 mask_index = find_lsb(free_masks);
      usize first_block =
          find_aligned_ones<N>(allocator->free_blocks[mask_index]);
      if (first_block != (u64)-1) {
        u64 block_mask = (u64(1) << N) - 1;
        block_mask = block_mask << first_block;
        ren_assert((allocator->free_blocks[mask_index] & block_mask) ==
                   block_mask);
        allocator->free_blocks[mask_index] &= ~block_mask;
        bool mask_empty = allocator->free_blocks[mask_index] == 0;
        allocator->free_masks &= ~(u64(mask_empty) << mask_index);
        return mask_index * 64 + first_block;
      }
      free_masks = free_masks & ~(u64(1) << mask_index);
    }
  } else {
    usize num_masks = N / 64;
    for (usize first_mask = 0; first_mask < 32; first_mask += num_masks) {
      bool all_free = true;
      for (usize j : range<usize>(0, num_masks)) {
        all_free =
            allocator->free_blocks[first_mask + j] == (u64)-1 and all_free;
      }
      if (all_free) {
        for (usize j : range<usize>(0, num_masks)) {
          ren_assert(allocator->free_blocks[first_mask + j] == (u64)-1);
          allocator->free_blocks[first_mask + j] = 0;
        }
        u64 masks_mask = (u64(1) << num_masks) - 1;
        allocator->free_masks &= ~(masks_mask << first_mask);
        return first_mask * 64;
      }
    }
  }
  fmt::println(stderr, "Thread allocator overflow");
  ren_trap();
  std::abort();
}

static void *allocate_block_slow(NotNull<BlockAllocator *> allocator,
                                 usize size) {
  ZoneScoped;
  usize min_block_size_bits = find_msb(allocator->min_block_size);
  usize num_blocks = size >> min_block_size_bits;
  usize first_block = [&]() {
    switch (num_blocks) {
    case 2:
      return find_first_block<2>(allocator);
    case 4:
      return find_first_block<4>(allocator);
    case 8:
      return find_first_block<8>(allocator);
    case 16:
      return find_first_block<16>(allocator);
    case 32:
      return find_first_block<32>(allocator);
    case 64:
      return find_first_block<64>(allocator);
    case 2 * 64:
      return find_first_block<2 * 64>(allocator);
    case 4 * 64:
      return find_first_block<4 * 64>(allocator);
    case 8 * 64:
      return find_first_block<8 * 64>(allocator);
    case 16 * 64:
      return find_first_block<16 * 64>(allocator);
    case 32 * 64:
      return find_first_block<32 * 64>(allocator);
    }
    std::unreachable();
  }();
  usize offset = first_block * allocator->min_block_size;
  [[unlikely]] if (offset + size > allocator->commit_size) {
    vm_commit((u8 *)allocator->pool + allocator->commit_size,
              offset + size - allocator->commit_size);
    allocator->commit_size = offset + size;
  }
  return (u8 *)allocator->pool + offset;
}

void *allocate_block(NotNull<BlockAllocator *> allocator, usize size) {
  ZoneScoped;
  [[likely]] if (size == allocator->min_block_size) {
    usize free_mask = find_lsb(allocator->free_masks);
    [[unlikely]] if (free_mask == (u64)-1) {
      fmt::println(stderr, "Thread allocator overflow");
      ren_trap();
      std::abort();
    }
    usize free_block = find_lsb(allocator->free_blocks[free_mask]);
    ren_assert(free_block != (u64)-1);
    ren_assert(allocator->free_blocks[free_mask] & (u64(1) << free_block));
    allocator->free_blocks[free_mask] &= ~(u64(1) << free_block);
    bool mask_empty = allocator->free_blocks[free_mask] == 0;
    allocator->free_masks &= ~(u64(mask_empty) << free_mask);
    usize offset = (free_mask * 64 + free_block) * allocator->min_block_size;
    [[unlikely]] if ((offset + size) > allocator->commit_size) {
      vm_commit((u8 *)allocator->pool + allocator->commit_size,
                offset + size - allocator->commit_size);
      allocator->commit_size = offset + size;
    }
    return (u8 *)allocator->pool + offset;
  }
  return allocate_block_slow(allocator, size);
}

void free_block(NotNull<BlockAllocator *> allocator, void *block, usize size) {
  usize offset = (u8 *)block - (u8 *)allocator->pool;
  ren_assert(offset + size < allocator->pool_size);
  ren_assert((offset & (size - 1)) == 0);
  usize min_block_size_bits = find_msb(allocator->min_block_size);
  usize first_block = offset >> min_block_size_bits;
  usize num_blocks = size >> min_block_size_bits;
  [[likely]] if (num_blocks <= 32) {
    usize mask_index = first_block / 64;
    first_block = first_block % 64;
    usize block_mask = (u64(1) << num_blocks) - 1;
    block_mask = block_mask << first_block;
    ren_assert((allocator->free_blocks[mask_index] & block_mask) == 0);
    allocator->free_blocks[mask_index] |= block_mask;
    allocator->free_masks |= u64(1) << mask_index;
  } else {
    usize first_mask = first_block / 64;
    usize num_masks = num_blocks / 64;
    for (usize mask_index : range(num_masks)) {
      ren_assert(allocator->free_blocks[first_mask + mask_index] == 0);
      allocator->free_blocks[first_mask + mask_index] = -1;
    }
    u64 masks_mask = (u64(1) << num_masks) - 1;
    allocator->free_masks |= masks_mask << first_mask;
  }
}

} // namespace ren
