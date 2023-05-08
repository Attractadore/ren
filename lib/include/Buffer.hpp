#pragma once
#include "DebugNames.hpp"
#include "Handle.hpp"
#include "Support/Optional.hpp"

#include <vulkan/vulkan.h>

namespace ren {

class Device;

enum class BufferHeap {
  Device,
  Upload,
  Readback,
};

struct BufferCreateInfo {
  REN_DEBUG_NAME_FIELD("Buffer");
  BufferHeap heap = BufferHeap::Device;
  VkBufferUsageFlags usage = 0;
  size_t size;
};

struct BufferView;

struct Buffer {
  VkBuffer handle;
  VmaAllocation allocation;
  std::byte *ptr;
  uint64_t address;
  size_t size;
  BufferHeap heap;
  VkBufferUsageFlags usage;

public:
  operator BufferView() const &;
};

struct BufferView {
  std::reference_wrapper<const Buffer> buffer;
  size_t offset = 0;
  size_t size = 0;

public:
  operator const Buffer &() const;

  auto operator->() const -> const Buffer *;

  auto get_descriptor() const -> VkDescriptorBufferInfo;

  template <typename T = std::byte>
  auto map(size_t map_offset = 0) const -> T * {
    const auto &buffer = this->buffer.get();
    assert(buffer.ptr);
    return (T *)(buffer.ptr + offset + map_offset);
  }
};

struct BufferHandleView {
  Handle<Buffer> buffer;
  size_t offset = 0;
  size_t size = 0;

public:
  auto try_get(const Device &device) const -> Optional<BufferView>;

  auto get(const Device &device) const -> BufferView;

  operator Handle<Buffer>() const;

  auto subbuffer(size_t offset, size_t size) const -> BufferHandleView;

  auto subbuffer(size_t offset) const -> BufferHandleView;
};

} // namespace ren
