#pragma once
#include "Support/Enum.hpp"
#include "Support/Ref.hpp"

#include <span>

namespace ren {
#define REN_BUFFER_USAGES                                                      \
  (TransferSRC)(TransferDST)(Uniform)(Storage)(Index)(Indirect)(DeviceAddress)
REN_DEFINE_FLAGS_ENUM(BufferUsage, REN_BUFFER_USAGES);

enum class BufferLocation {
  Device,
  Host,
  HostCached,
};

struct BufferDesc {
  BufferUsageFlags usage;
  BufferLocation location = BufferLocation::Device;
  unsigned offset = 0;
  unsigned size;
  void *ptr = nullptr;
};

namespace detail {
template <typename B> class BufferMixin {
  const B &get() const { return *static_cast<const B *>(this); }
  B &get() { return *static_cast<B *>(this); }

public:
  template <typename T = std::byte> T *map(unsigned offset = 0) const {
    if (get().desc.ptr) {
      return reinterpret_cast<T *>(
          reinterpret_cast<std::byte *>(get().desc.ptr) +
          (get().desc.offset + offset));
    }
    return nullptr;
  }

  template <typename T = std::byte>
  std::span<T> map(unsigned offset, unsigned count) const {
    assert(get().desc.ptr);
    return {map<T>(offset), count};
  }
};
} // namespace detail

struct BufferRef : detail::BufferMixin<BufferRef> {
  BufferDesc desc;
  void *handle;

  void *get() const { return handle; }
};

struct Buffer : detail::BufferMixin<Buffer> {
  BufferDesc desc;
  AnyRef handle;

  operator BufferRef() const {
    return {
        .desc = desc,
        .handle = handle.get(),
    };
  }

  void *get() const { return handle.get(); }
};

template <typename T>
concept BufferLike = std::same_as<T, Buffer> or std::same_as<T, BufferRef>;
} // namespace ren
