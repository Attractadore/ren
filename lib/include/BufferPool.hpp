#pragma once
#include "Buffer.hpp"
#include "Device.hpp"
#include "Errors.hpp"
#include "Support/StackAllocatorPool.hpp"

#include <utility>

namespace ren {

class BufferPool {
  Device *m_device;
  BufferDesc m_buffer_desc;
  Vector<Buffer> m_buffers;
  StackAllocatorPool m_allocator;

  Buffer &create_buffer(unsigned size) {
    auto desc = m_buffer_desc;
    desc.size = std::max(desc.size, size);
    return m_buffers.emplace_back(m_device->create_buffer(desc));
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

    const BufferRef &get() const { return m_buffer; }
  };

  BufferRef allocate(unsigned size, unsigned alignment = 4) {
    auto [allocation, offset] = m_allocator.allocate(size, alignment);
    auto [idx, _] = allocation;
    if (idx == m_buffers.size()) {
      assert(offset == 0);
      return create_buffer(size).subbuffer(0, size);
    } else {
      return m_buffers[idx].subbuffer(offset, size);
    }
  }

  UniqueAllocation allocate_unique(unsigned size, unsigned alignment = 4) {
    return {this, allocate(size, alignment)};
  }

  void free(BufferRef buffer) {
    for (unsigned i = 0; i < m_buffers.size(); ++i) {
      if (m_buffers[i].handle.get() == buffer.handle) {
        m_device->push_to_delete_queue(
            [this, i, size = buffer.desc.size](Device &) {
              m_allocator.free({.idx = i, .count = size});
            });
        return;
      }
    }
    unreachable("Failed to find parent for buffer suballocated from pool");
  }
};

} // namespace ren
