#pragma once
#include "Buffer.hpp"

#include <glm/vec3.hpp>

#include <limits>

namespace ren {
inline constexpr unsigned ATTRIBUTE_UNUSED = -1;

struct Mesh {
  using color_t = uint32_t;
  using index_t = unsigned;

  BufferRef vertex_allocation;
  BufferRef index_allocation;
  unsigned num_vertices;
  unsigned num_indices;
  unsigned colors_offset = ATTRIBUTE_UNUSED;
};

template <unsigned bits>
unsigned encode_float(float f, float from = 0.0f, float to = 1.0f) {
  assert(from <= f and f <= to);
  return (f - from) / (to - from) * ((1 << bits) - 1);
}

inline Mesh::color_t encode_color(glm::vec3 color) {
  constexpr unsigned red_bits = 11;
  constexpr unsigned green_bits = 11;
  constexpr unsigned blue_bits = 10;
  static_assert(red_bits + green_bits + blue_bits <=
                std::numeric_limits<Mesh::color_t>::digits);
  return encode_float<red_bits>(color.r) |
         (encode_float<green_bits>(color.g) << red_bits) |
         (encode_float<blue_bits>(color.b) << (red_bits + green_bits));
}
} // namespace ren
