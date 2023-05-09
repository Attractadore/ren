#include "Buffer.hpp"
#include "Device.hpp"

namespace ren {

auto BufferHandleView::from_buffer(const Device &device, Handle<Buffer> buffer)
    -> BufferHandleView {
  return {
      .buffer = buffer,
      .size = device.get_buffer(buffer).size,
  };
}

auto BufferHandleView::get_descriptor(const Device &device) const
    -> VkDescriptorBufferInfo {
  return {
      .buffer = device.get_buffer(buffer).handle,
      .offset = offset,
      .range = size,
  };
}

BufferHandleView::operator Handle<Buffer>() const { return buffer; }

auto BufferHandleView::subbuffer(size_t offset, size_t size) const
    -> BufferHandleView {
  assert(offset <= this->size);
  assert(offset + size <= this->size);
  return {
      .buffer = buffer,
      .offset = this->offset + offset,
      .size = size,
  };
}

auto BufferHandleView::subbuffer(size_t offset) const -> BufferHandleView {
  assert(offset <= this->size);
  return subbuffer(offset, this->size - offset);
}

} // namespace ren
