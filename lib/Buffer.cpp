#include "Buffer.hpp"
#include "Device.hpp"

namespace ren {

auto Buffer::get_descriptor() const -> VkDescriptorBufferInfo {
  return {
      .buffer = handle,
      .range = size,
  };
}

auto BufferView::from_buffer(const Device &device, Handle<Buffer> buffer)
    -> BufferView {
  return {
      .buffer = buffer,
      .size = device.get_buffer(buffer).size,
  };
}

auto BufferView::get_descriptor(const Device &device) const
    -> VkDescriptorBufferInfo {
  return {
      .buffer = device.get_buffer(buffer).handle,
      .offset = offset,
      .range = size,
  };
}

BufferView::operator Handle<Buffer>() const { return buffer; }

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
