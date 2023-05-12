#pragma once
#include "Buffer.hpp"
#include "Device.hpp"

namespace ren {

template <typename T>
auto BufferView::map(const Device &device, usize map_offset) const -> T * {
  auto *ptr = device.get_buffer(buffer).ptr;
  if (!ptr)
    return nullptr;
  return (T *)(ptr + offset + map_offset);
}

} // namespace ren
