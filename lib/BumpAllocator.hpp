#pragma once
#include "Buffer.hpp"
#include "Config.hpp"
#include "Renderer.hpp"
#include "Support/Math.hpp"
#include "glsl/DevicePtr.h"

namespace ren {

namespace detail {

template <typename Policy> class BumpAllocator {
public:
  template <typename T = std::byte>
  using Allocation = Policy::template Allocation<T>;

  BumpAllocator(Renderer &renderer, usize block_size = 1 * 1024 * 1024) {
    m_renderer = &renderer;
    m_block_size = block_size;
  }

  BumpAllocator(const BumpAllocator &) = delete;
  BumpAllocator(BumpAllocator &&) = default;

  ~BumpAllocator() { destroy(); }

  BumpAllocator &operator=(const BumpAllocator &) = delete;

  BumpAllocator &operator=(BumpAllocator &&other) {
    destroy();
    m_block_ring = std::move(other.m_block_ring);
    m_block_size = other.m_block_size;
    m_block = other.m_block;
    other.m_block = 0;
    m_block_offset = other.m_block_offset;
    other.m_block_offset = 0;
    return *this;
  }

  template <typename T = std::byte>
  auto allocate(usize count) -> Allocation<T> {
    m_block_offset = pad(m_block_offset, alignof(T));
    m_block_offset = pad(m_block_offset, glsl::DEFAULT_DEVICE_PTR_ALIGNMENT);
    usize size = count * sizeof(T);

    Vector<Block> &blocks = m_block_ring.front();

    [[unlikely]] if (m_block_offset + size > m_block_size ||
                     m_block >= blocks.size()) {
      blocks.push_back(Policy::create_block(*m_renderer, m_block_size));
      m_block = blocks.size() - 1;
      m_block_offset = 0;
    }

    Allocation<T> allocation =
        Policy::template allocate<T>(blocks[m_block], m_block_offset, size);

    m_block_offset += size;

    return allocation;
  }

  void next_frame() {
    rotate_left(m_block_ring);
    m_block = 0;
    m_block_offset = 0;
  }

private:
  void destroy() {
    for (Vector<Block> &blocks : m_block_ring) {
      for (const Block &block : blocks) {
        m_renderer->destroy(block.buffer);
      }
      blocks.clear();
    }
  }

private:
  Renderer *m_renderer = nullptr;

  using Block = Policy::Block;

  std::array<Vector<Block>, PIPELINE_DEPTH> m_block_ring;
  usize m_block_size = 0;
  usize m_block = 0;
  usize m_block_offset = 0;
};

struct DeviceBumpAllocationPolicy {
  struct Block {
    DevicePtr<std::byte> ptr;
    Handle<Buffer> buffer;
  };

  static auto create_block(Renderer &renderer, usize size) -> Block {
    Handle<Buffer> buffer = renderer.create_buffer({
        .name = "DeviceBumpAllocator block",
        .heap = BufferHeap::Static,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .size = size,
    });
    return {
        .ptr = renderer.get_buffer_device_ptr<std::byte>(buffer),
        .buffer = buffer,
    };
  };

  template <typename T> struct Allocation {
    DevicePtr<T> ptr;
    BufferView view;
  };

  template <typename T>
  static auto allocate(const Block &block, usize offset, usize size)
      -> Allocation<T> {
    return {
        .ptr = DevicePtr<T>(block.ptr + offset),
        .view =
            {
                .buffer = block.buffer,
                .offset = offset,
                .size = size,
            },
    };
  }
};

struct UploadBumpAllocationPolicy {
  struct Block {
    std::byte *host_ptr = nullptr;
    DevicePtr<std::byte> device_ptr;
    Handle<Buffer> buffer;
  };

  template <typename T> struct Allocation {
    T *host_ptr = nullptr;
    DevicePtr<T> device_ptr;
    BufferView view;
  };

  static auto create_block(Renderer &renderer, usize size) -> Block {
    Handle<Buffer> buffer = renderer.create_buffer({
        .name = "UploadBumpAllocator block",
        .heap = BufferHeap::Staging,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .size = size,
    });
    return {
        .host_ptr = renderer.map_buffer<std::byte>(buffer),
        .device_ptr = renderer.get_buffer_device_ptr<std::byte>(buffer),
        .buffer = buffer,
    };
  };

  template <typename T>
  static auto allocate(const Block &block, usize offset, usize size)
      -> Allocation<T> {
    return {
        .host_ptr = (T *)(block.host_ptr + offset),
        .device_ptr = DevicePtr<T>(block.device_ptr + offset),
        .view =
            {
                .buffer = block.buffer,
                .offset = offset,
                .size = size,
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
