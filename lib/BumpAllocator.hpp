#pragma once
#include "Buffer.hpp"
#include "ResourceArena.hpp"
#include "core/Math.hpp"
#include "glsl/DevicePtr.h"

namespace ren {

class CommandRecorder;

namespace detail {

template <typename Policy> class BumpAllocator {
public:
  template <typename T = std::byte>
  using Allocation = Policy::template Allocation<T>;

  BumpAllocator(Renderer &renderer, ResourceArena &arena, usize block_size) {
    m_renderer = &renderer;
    m_arena = &arena;
    m_block_size = block_size;
    reset();
  }

  BumpAllocator(const BumpAllocator &) = delete;
  BumpAllocator(BumpAllocator &&) = default;

  ~BumpAllocator() = default;

  BumpAllocator &operator=(const BumpAllocator &) = delete;

  BumpAllocator &operator=(BumpAllocator &&other) {
    m_renderer = other.m_renderer;
    m_arena = other.m_arena;
    m_blocks = std::move(other.m_blocks);
    m_block_size = other.m_block_size;
    m_block = other.m_block;
    m_block_offset = other.m_block_offset;
    other.reset();
    return *this;
  }

  template <typename T = std::byte>
  auto allocate(usize count) -> Allocation<T> {
    m_block_offset = pad(m_block_offset, alignof(T));
    m_block_offset = pad(m_block_offset, Policy::ALIGNMENT);
    usize size = count * sizeof(T);
    ren_assert(size > 0);
    ren_assert(size <= m_block_size);

    [[unlikely]] if (m_block_offset + size > m_block_size) {
      m_block += 1;
      [[unlikely]] if (m_block == m_blocks.size()) {
        // FIXME: check for error.
        m_blocks.push_back(
            Policy::create_block(*m_renderer, *m_arena, m_block_size).value());
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

private:
  using Block = Policy::Block;
  Renderer *m_renderer = nullptr;
  ResourceArena *m_arena = nullptr;
  SmallVector<Block, 1> m_blocks;
  usize m_block_size = 0;
  usize m_block = 0;
  usize m_block_offset = 0;
};

struct DeviceBumpAllocationPolicy {
  static constexpr usize ALIGNMENT = glsl::DEFAULT_DEVICE_PTR_ALIGNMENT;

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
      std::max<usize>(glsl::DEFAULT_DEVICE_PTR_ALIGNMENT, 64);

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
