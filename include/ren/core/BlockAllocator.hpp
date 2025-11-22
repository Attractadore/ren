#pragma once
#include "NotNull.hpp"
#include "StdDef.hpp"

namespace ren {

struct BlockAllocator {
  // Mask of free masks.
  u64 free_masks = 0;
  // Masks of free blocks.
  u64 free_blocks[64] = {};
  usize min_block_size = 0;
  void *pool = nullptr;
  usize pool_size = 0;
  usize commit_size = 0;
};

void init_allocator(NotNull<BlockAllocator *> allocator, usize block_size);

void destroy_allocator(NotNull<BlockAllocator *> allocator);

void *allocate_block(NotNull<BlockAllocator *> allocator, usize size);

void free_block(NotNull<BlockAllocator *> allocator, void *block, usize size);

} // namespace ren
