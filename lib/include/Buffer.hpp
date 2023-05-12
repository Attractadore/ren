#pragma once
#include "DebugNames.hpp"
#include "Handle.hpp"
#include "Support/Optional.hpp"
#include "Support/StdDef.hpp"

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
  static auto try_from_buffer(const Device &device, Handle<Buffer> buffer)
      -> Optional<BufferView>;

  static auto from_buffer(const Device &device, Handle<Buffer> buffer)
      -> BufferView;

  template <typename T = std::byte>
  auto map(const Device &device, usize map_offset = 0) const -> T *;

  auto get_address(const Device &device, u64 map_offset = 0) const -> u64;

  auto subbuffer(usize offset, usize size) const -> BufferView;

  auto subbuffer(usize offset) const -> BufferView;
};

} // namespace ren
