#pragma once
#include "Buffer.hpp"

namespace ren {
inline constexpr unsigned ATTRIBUTE_UNUSED = -1;

struct Mesh {
  BufferRef vertex_allocation;
  BufferRef index_allocation;
  unsigned num_vertices;
  unsigned num_indices;
  unsigned colors_offset = ATTRIBUTE_UNUSED;
};
} // namespace ren
