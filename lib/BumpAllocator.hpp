#pragma once
#include "Buffer.hpp"
#include "DevicePtr.hpp"
#include "ResourceArena.hpp"
#include "core/Math.hpp"
#include "sh/Std.h"

namespace ren {

namespace detail {

constexpr usize MAX_BUMP_ALLOCATOR_BLOCKS = 8;

template <typename Policy> struct BumpAllocator {
  using Block = Policy::Block;

  Renderer *m_renderer = nullptr;
  ResourceArena *m_arena = nullptr;
  Block m_blocks[MAX_BUMP_ALLOCATOR_BLOCKS] = {};
  usize m_num_blocks = 0;
  usize m_block_size = 0;
  usize m_block = 0;
  usize m_block_offset = 0;

public:
  [[nodiscard]] static BumpAllocator
  init(Renderer &renderer, ResourceArena &gfx_arena, usize block_size) {
    return {
        .m_renderer = &renderer,
        .m_arena = &gfx_arena,
        .m_block_size = block_size,
        .m_block = (usize)-1,
        .m_block_offset = block_size,
    };
  }

  template <typename T = std::byte>
  using Allocation = Policy::template Allocation<T>;

  template <typename T = std::byte>
  auto allocate(usize count) -> Allocation<T> {
    m_block_offset = pad(m_block_offset, alignof(T));
    m_block_offset = pad(m_block_offset, Policy::ALIGNMENT);
    usize size = count * sizeof(T);
    ren_assert(size > 0);
    ren_assert(size <= m_block_size);

    [[unlikely]] if (m_block_offset + size > m_block_size) {
      m_block += 1;
      [[unlikely]] if (m_block == m_num_blocks) {
        ren_assert(m_block < MAX_BUMP_ALLOCATOR_BLOCKS);
        // FIXME: check for error.
        m_blocks[m_num_blocks++] =
            Policy::create_block(*m_renderer, *m_arena, m_block_size).value();
      }
      m_block_offset = 0;
    }

    Allocation<T> allocation =
        Policy::template allocate<T>(m_blocks[m_block], m_block_offset, count);

    m_block_offset += size;

    return allocation;
  }

  void reset() {
    m_block = -1;
    m_block_offset = m_block_size;
  }
};

struct DeviceBumpAllocationPolicy {
  static constexpr usize ALIGNMENT = sh::DEFAULT_DEVICE_PTR_ALIGNMENT;

  struct Block {
    DevicePtr<std::byte> ptr;
    Handle<Buffer> buffer;
  };

  static auto create_block(Renderer &renderer, ResourceArena &arena, usize size)
      -> Result<Block, Error> {
    ren_try(BufferView view, arena.create_buffer({
                                 .name = "DeviceBumpAllocator block",
                                 .heap = rhi::MemoryHeap::Default,
                                 .size = size,
                             }));
    return Block{
        .ptr = renderer.get_buffer_device_ptr(view),
        .buffer = view.buffer,
    };
  };

  template <typename T> struct Allocation {
    DevicePtr<T> ptr;
    BufferSlice<T> slice;
  };

  template <typename T>
  static auto allocate(const Block &block, usize offset, usize count)
      -> Allocation<T> {
    return {
        .ptr = DevicePtr<T>(block.ptr + offset),
        .slice =
            {
                .buffer = block.buffer,
                .offset = offset,
                .count = count,
            },
    };
  }
};

struct UploadBumpAllocationPolicy {
  static constexpr auto ALIGNMENT =
      std::max<usize>(sh::DEFAULT_DEVICE_PTR_ALIGNMENT, 64);

  struct Block {
    std::byte *host_ptr = nullptr;
    DevicePtr<std::byte> device_ptr;
    Handle<Buffer> buffer;
  };

  template <typename T> struct Allocation {
    T *host_ptr = nullptr;
    DevicePtr<T> device_ptr;
    BufferSlice<T> slice;
  };

  static auto create_block(Renderer &renderer, ResourceArena &arena, usize size)
      -> Result<Block, Error> {
    ren_try(BufferView view, arena.create_buffer({
                                 .name = "UploadBumpAllocator block",
                                 .heap = rhi::MemoryHeap::Upload,
                                 .size = size,
                             }));
    return Block{
        .host_ptr = renderer.map_buffer(view),
        .device_ptr = renderer.get_buffer_device_ptr(view),
        .buffer = view.buffer,
    };
  };

  template <typename T>
  static auto allocate(const Block &block, usize offset, usize count)
      -> Allocation<T> {
    return {
        .host_ptr = (T *)(block.host_ptr + offset),
        .device_ptr = DevicePtr<T>(block.device_ptr + offset),
        .slice =
            {
                .buffer = block.buffer,
                .offset = offset,
                .count = count,
            },
    };
  }
};

}; // namespace detail

using DeviceBumpAllocator =
    detail::BumpAllocator<detail::DeviceBumpAllocationPolicy>;

using UploadBumpAllocator =
    detail::BumpAllocator<detail::UploadBumpAllocationPolicy>;

template <typename T>
using DeviceBumpAllocation = DeviceBumpAllocator::Allocation<T>;

template <typename T>
using UploadBumpAllocation = UploadBumpAllocator::Allocation<T>;

} // namespace ren
