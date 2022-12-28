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

struct Buffer {
  BufferDesc desc;
  AnyRef handle;
};

struct BufferRef {
  BufferDesc desc;
  void *handle;
};

template <typename T = std::byte>
T *get_host_ptr(const BufferRef &buffer, unsigned offset = 0) {
  return reinterpret_cast<T *>(reinterpret_cast<std::byte *>(buffer.desc.ptr) +
                               (buffer.desc.offset + offset));
}

template <typename T = std::byte>
std::span<T> get_host_ptr(const BufferRef &buffer, unsigned offset,
                          unsigned count) {
  return {get_host_ptr<T>(buffer, offset), count};
}
} // namespace ren
