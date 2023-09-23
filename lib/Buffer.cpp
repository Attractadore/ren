#include "Buffer.hpp"

namespace ren {

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
