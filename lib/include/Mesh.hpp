#pragma once
#include "Buffer.hpp"
#include "Formats.hpp"

#include <array>

namespace ren {
inline constexpr unsigned ATTRIBUTE_UNUSED = -1;

enum MeshAttribute {
  MESH_ATTRIBUTE_POSITIONS = 0,
  MESH_ATTRIBUTE_NORMALS,
  MESH_ATTRIBUTE_COLORS,
  MESH_ATTRIBUTE_UVS,
  MESH_ATTRIBUTE_COUNT,
};

struct Mesh {
  Handle<Buffer> vertex_buffer;
  Handle<Buffer> index_buffer;
  unsigned num_vertices;
  unsigned num_indices;
  VkIndexType index_format;
  std::array<unsigned, MESH_ATTRIBUTE_COUNT> attribute_offsets;
};
} // namespace ren
