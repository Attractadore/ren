#pragma once
#include "Buffer.hpp"
#include "Formats.hpp"

namespace ren {
inline constexpr unsigned ATTRIBUTE_UNUSED = -1;

struct Mesh {
  BufferRef vertex_allocation;
  BufferRef index_allocation;
  unsigned num_vertices;
  unsigned num_indices;
  unsigned positions_offset;
  unsigned colors_offset = ATTRIBUTE_UNUSED;
  IndexFormat index_format;
};
} // namespace ren
