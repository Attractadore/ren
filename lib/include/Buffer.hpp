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
  auto get_descriptor() const -> VkDescriptorBufferInfo;

  template <typename T = std::byte> auto map(size_t offset = 0) const -> T * {
    if (ptr) {
      return (T *)(ptr + offset);
    }
    return nullptr;
  }
};

struct BufferView {
  Handle<Buffer> buffer;
  size_t offset = 0;
  size_t size = 0;

public:
  static auto from_buffer(const Device &device, Handle<Buffer> buffer)
      -> BufferView;

  operator Handle<Buffer>() const;

  auto get_descriptor(const Device &device) const -> VkDescriptorBufferInfo;

  auto subbuffer(size_t offset, size_t size) const -> BufferView;

  auto subbuffer(size_t offset) const -> BufferView;
};

} // namespace ren
