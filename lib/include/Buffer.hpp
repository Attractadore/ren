#pragma once
#include "Support/Handle.hpp"

#include <vulkan/vulkan.h>

#include <span>

namespace ren {

enum class BufferHeap {
  Device,
  Upload,
  Readback,
};

struct BufferDesc {
  VkBufferUsageFlags usage = 0;
  BufferHeap heap = BufferHeap::Device;
  unsigned offset = 0;
  unsigned size;
  void *ptr = nullptr;
  uint64_t gpu_addr = 0;

  bool operator==(const BufferDesc &other) const = default;
};

namespace detail {
template <typename B> class BufferMixin {
  const B &impl() const { return *static_cast<const B *>(this); }
  B &impl() { return *static_cast<B *>(this); }

public:
  template <typename T = std::byte> T *map() const {
    return reinterpret_cast<T *>(impl().desc.ptr);
  }

  template <typename T = std::byte> std::span<T> map(unsigned count) const {
    assert(impl().desc.ptr);
    return {map<T>(), count};
  }

  template <typename T = std::byte>
  B subbuffer(unsigned offset, unsigned count) const {
    auto sb = impl();
    auto size = count * sizeof(T);
    assert(offset + size <= sb.desc.size);
    sb.desc.offset += offset;
    sb.desc.size = size;
    if (sb.desc.ptr) {
      sb.desc.ptr = reinterpret_cast<std::byte *>(sb.desc.ptr) + offset;
    }
    if (sb.desc.gpu_addr) {
      sb.desc.gpu_addr += offset;
    }
    return sb;
  }

  B subbuffer(unsigned offset) const {
    const auto &desc = impl().desc;
    assert(offset <= desc.size);
    return subbuffer(offset, desc.size - offset);
  }

  VkDescriptorBufferInfo get_descriptor() const {
    return {
        .buffer = impl().get(),
        .offset = impl().desc.offset,
        .range = impl().desc.size,
    };
  }

  bool operator==(const BufferMixin &other) const {
    const auto &lhs = impl();
    const auto &rhs = other.impl();
    return lhs.get() == rhs.get() and lhs.desc == rhs.desc;
  };
};
} // namespace detail

struct BufferRef : detail::BufferMixin<BufferRef> {
  BufferDesc desc;
  VkBuffer handle;

  VkBuffer get() const { return handle; }
};

struct Buffer : detail::BufferMixin<Buffer> {
  BufferDesc desc;
  SharedHandle<VkBuffer> handle;

  operator BufferRef() const {
    return {
        .desc = desc,
        .handle = handle.get(),
    };
  }

  VkBuffer get() const { return handle.get(); }
};

} // namespace ren
