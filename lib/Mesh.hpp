#pragma once
#include "Handle.hpp"
#include "Support/Enum.hpp"
#include "Support/StdDef.hpp"
#include "glsl/Mesh.hpp"

#include <glm/glm.hpp>

namespace ren {

struct Buffer;

enum class MeshAttribute {
  UV = glsl::MESH_ATTRIBUTE_UV_BIT,
  Tangent = glsl::MESH_ATTRIBUTE_TANGENT_BIT,
  Color = glsl::MESH_ATTRIBUTE_COLOR_BIT,
};
ENABLE_FLAGS(MeshAttribute);

struct Mesh {
  MeshAttributeFlags attributes;
  u32 pool = -1;
  glsl::PositionBoundingBox bounding_box = {
      .min = {glm::i16vec3(std::numeric_limits<i16>::max())},
      .max = {glm::i16vec3(std::numeric_limits<i16>::min())},
  };
  // Select relatively big default bounding box size to avoid log2 NaN
  glm::vec3 position_encode_bounding_box = glm::vec3(1.0f);
  glsl::BoundingSquare uv_bounding_square;
  u32 base_vertex = 0;
  u32 base_index = 0;
  u32 num_indices = 0;
  Vector<glsl::MeshLOD> lods;
};

struct VertexPool {
  AutoHandle<Buffer> positions;
  AutoHandle<Buffer> normals;
  AutoHandle<Buffer> tangents;
  AutoHandle<Buffer> colors;
  AutoHandle<Buffer> uvs;
  AutoHandle<Buffer> indices;
  u32 num_free_vertices = glsl::NUM_VERTEX_POOL_VERTICES;
  u32 num_free_indices = glsl::NUM_VERTEX_POOL_INDICES;
};

using VertexPoolList = SmallVector<VertexPool, 1>;

auto create_vertex_pool(MeshAttributeFlags attributes) -> VertexPool;

struct MeshInstance {
  u32 mesh = 0;
  u32 material = 0;
  glm::mat4x3 matrix{1.0f};
};

} // namespace ren
