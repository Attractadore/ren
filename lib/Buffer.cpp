#include "Buffer.hpp"
#include "Device.hpp"

namespace ren {

Buffer::operator BufferView() const & {
  return BufferView{
      .buffer = *this,
      .size = size,
  };
}

BufferView::operator const Buffer &() const { return buffer; }

auto BufferView::operator->() const -> const Buffer * { return &buffer.get(); }

auto BufferView::get_descriptor() const -> VkDescriptorBufferInfo {
  return {
      .buffer = buffer.get().handle,
      .offset = offset,
      .range = size,
  };
}

auto BufferHandleView::try_get(const Device &device) const
    -> Optional<BufferView> {
  return device.try_get_buffer(buffer).map([&](const Buffer &buffer) {
    return BufferView{
        .buffer = buffer,
        .offset = offset,
        .size = size,
    };
  });
}

auto BufferHandleView::get(const Device &device) const -> BufferView {
  return {
      .buffer = device.get_buffer(buffer),
      .offset = offset,
      .size = size,
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
