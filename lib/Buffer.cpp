#include "Buffer.hpp"
#include "Device.hpp"

namespace ren {

auto BufferView::try_from_buffer(const Device &device, Handle<Buffer> handle)
    -> Optional<BufferView> {
  return device.try_get_buffer(handle).map([&](const Buffer &buffer) {
    return BufferView{
        .buffer = handle,
        .size = buffer.size,
    };
  });
};

auto BufferView::from_buffer(const Device &device, Handle<Buffer> handle)
    -> BufferView {
  const auto &buffer = device.get_buffer(handle);
  return {
      .buffer = handle,
      .size = buffer.size,
  };
};

auto BufferView::get_address(const Device &device, u64 map_offset) const
    -> u64 {
  auto addr = device.get_buffer(buffer).address;
  if (!addr)
    return 0;
  return addr + offset + map_offset;
}

auto BufferView::subbuffer(size_t offset, size_t size) const -> BufferView {
  assert(offset <= this->size);
  assert(offset + size <= this->size);
  return {
      .buffer = buffer,
      .offset = this->offset + offset,
      .size = size,
  };
}

auto BufferView::subbuffer(size_t offset) const -> BufferView {
  assert(offset <= this->size);
  return subbuffer(offset, this->size - offset);
}

} // namespace ren
