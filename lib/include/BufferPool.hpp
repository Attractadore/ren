#pragma once
#include "Buffer.hpp"
#include "Device.hpp"
#include "Support/StackAllocatorPool.hpp"

#include <range/v3/algorithm.hpp>

#include <utility>

namespace ren {
class BufferPool;

class BufferPool {
  Device *m_device;
  BufferDesc m_buffer_desc;
  SmallVector<Buffer, 8> m_buffers;
  StackAllocatorPool m_allocator;

  void create_buffer(unsigned size) {
    auto desc = m_buffer_desc;
    desc.size = std::max(desc.size, size);
    m_buffers.emplace_back(m_device->create_buffer(desc));
  }

public:
  BufferPool(Device *device, const BufferDesc &buffer_desc)
      : m_device(device), m_buffer_desc(buffer_desc),
        m_allocator(buffer_desc.size) {}

  class UniqueAllocation {
    BufferPool *m_parent;
    BufferRef m_buffer;

  private:
    void destroy() {
      if (m_buffer.handle) {
        assert(m_parent);
        m_parent->free(m_buffer);
      }
    }

  public:
    UniqueAllocation(BufferPool *parent, BufferRef buffer)
        : m_parent(parent), m_buffer(buffer) {}
    UniqueAllocation(const UniqueAllocation &) = delete;
    UniqueAllocation(UniqueAllocation &&other)
        : m_parent(std::exchange(other.m_parent, nullptr)),
          m_buffer(std::exchange(other.m_buffer, {})) {}
    UniqueAllocation &operator=(const UniqueAllocation &) = delete;
    UniqueAllocation &operator=(UniqueAllocation &&other) {
      destroy();
      m_parent = other.m_parent;
      other.m_parent = nullptr;
      m_buffer = other.m_buffer;
      other.m_buffer = {};
      return *this;
    }
    ~UniqueAllocation() { destroy(); }

    const BufferRef &get_buffer() const { return m_buffer; }
  };

  BufferRef allocate(unsigned size, unsigned alignment = 4) {
    auto [allocation, offset] = m_allocator.allocate(size, alignment);
    auto [idx, _] = allocation;
    if (idx == m_buffers.size()) {
      create_buffer(size);
    }
    assert(idx < m_buffers.size());
    auto desc = m_buffer_desc;
    desc.offset = offset;
    desc.size = size;
    return {.desc = desc, .handle = m_buffers[idx].handle.get()};
  }

  UniqueAllocation allocate_unique(unsigned size, unsigned alignment = 4) {
    return {this, allocate(size, alignment)};
  }

  void free(BufferRef buffer_ref) {
    unsigned idx =
        ranges::distance(m_buffers.begin(),
                         ranges::find_if(m_buffers, [&](const Buffer &buffer) {
                           return buffer.handle.get() == buffer_ref.handle;
                         }));
    assert(idx < m_buffers.size());
    m_device->push_to_delete_queue(
        [this, idx, size = buffer_ref.desc.size](Device &) {
          m_allocator.free({.idx = idx, .count = size});
        });
  }
};
} // namespace ren
