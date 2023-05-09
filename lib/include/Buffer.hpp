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

struct Buffer {
  VkBuffer handle;
  VmaAllocation allocation;
  std::byte *ptr;
  uint64_t address;
  size_t size;
  BufferHeap heap;
  VkBufferUsageFlags usage;

public:
  template <typename T = std::byte> auto map(size_t offset = 0) const -> T * {
    assert(ptr);
    return (T *)(ptr + offset);
  }
};

struct BufferHandleView {
  Handle<Buffer> buffer;
  size_t offset = 0;
  size_t size = 0;

public:
  static auto from_buffer(const Device &device, Handle<Buffer> buffer)
      -> BufferHandleView;

  operator Handle<Buffer>() const;

  auto get_descriptor(const Device &device) const -> VkDescriptorBufferInfo;

  auto subbuffer(size_t offset, size_t size) const -> BufferHandleView;

  auto subbuffer(size_t offset) const -> BufferHandleView;
};

} // namespace ren
