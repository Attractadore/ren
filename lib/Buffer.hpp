#pragma once
#include "DebugNames.hpp"
#include "Handle.hpp"
#include "Support/StdDef.hpp"
#include "Support/Hash.hpp"

#include <vk_mem_alloc.h>

namespace ren {

enum class BufferHeap {
  Static,
  Dynamic,
  Staging,
  Readback,
};
constexpr usize NUM_BUFFER_HEAPS = 4;

struct BufferCreateInfo {
  REN_DEBUG_NAME_FIELD("Buffer");
  BufferHeap heap = BufferHeap::Static;
  VkBufferUsageFlags usage = 0;
  usize size;
};

struct Buffer {
  VkBuffer handle;
  VmaAllocation allocation;
  std::byte *ptr;
  u64 address;
  usize size;
  BufferHeap heap;
  VkBufferUsageFlags usage;
};

struct BufferView {
  Handle<Buffer> buffer;
  usize offset = 0;
  usize size = 0;

public:
  auto subbuffer(usize offset, usize size) const -> BufferView;

  auto subbuffer(usize offset) const -> BufferView;

  template <typename T>
  auto slice(usize start, usize count) const -> BufferView {
    return subbuffer(sizeof(T) * start, sizeof(T) * count);
  }

  template <typename T> auto slice(usize start) const -> BufferView {
    return subbuffer(sizeof(T) * start);
  }

  bool operator==(const BufferView&) const = default;
};

REN_DEFINE_TYPE_HASH(BufferView, buffer, offset, size);


} // namespace ren
