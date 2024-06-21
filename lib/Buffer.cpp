#include "Buffer.hpp"
#include "Support/Assert.hpp"

namespace ren {

auto BufferView::subbuffer(size_t offset, size_t size) const -> BufferView {
  ren_assert(offset <= this->size);
  ren_assert(offset + size <= this->size);
  return {
      .buffer = buffer,
      .offset = this->offset + offset,
      .size = size,
  };
}

auto BufferView::subbuffer(size_t offset) const -> BufferView {
  ren_assert(offset <= this->size);
  return subbuffer(offset, this->size - offset);
}

} // namespace ren
