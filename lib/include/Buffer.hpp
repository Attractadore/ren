#pragma once
#include "Support/Enum.hpp"
#include "Support/Ref.hpp"

#include <span>

namespace ren {
#define REN_BUFFER_USAGES                                                      \
  (TransferSRC)(TransferDST)(                                                  \
      Uniform)(Storage)(Index)(Indirect)(DeviceAddress)(HostMapped)(Readback)
REN_DEFINE_FLAGS_ENUM(BufferUsage, REN_BUFFER_USAGES);

struct BufferDesc {
  BufferUsageFlags usage;
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
