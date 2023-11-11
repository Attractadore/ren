#pragma once
#include "Handle.hpp"
#include "Support/Enum.hpp"
#include "Support/StdDef.hpp"
#include "glsl/Mesh.hpp"

#include <glm/glm.hpp>

namespace ren {

struct Buffer;

#define MESH_ATTRIBUTES (Color)(Tangent)(UV)
REN_DEFINE_FLAGS_ENUM(MeshAttribute, MESH_ATTRIBUTES);

constexpr usize NUM_MESH_ATTRIBUTE_FLAGS =
    (usize)(MeshAttribute::Color | MeshAttribute::Tangent | MeshAttribute::UV)
        .get() +
    1;

struct Mesh {
  MeshAttributeFlags attributes;
  u32 pool = -1;
  u32 base_vertex = 0;
  u32 base_index = 0;
  u32 num_indices = 0;
  // Select relatively big default bounding box size to avoid log2 NaN
  glm::vec3 position_encode_bounding_box = glm::vec3(1.0f);
  glsl::BoundingSquare uv_bounding_square;
};

constexpr u32 NUM_VERTEX_POOL_VERTICES = 1 << 20;
constexpr u32 NUM_VERTEX_POOL_INDICES = 5 * NUM_VERTEX_POOL_VERTICES;

struct VertexPool {
  AutoHandle<Buffer> positions;
  AutoHandle<Buffer> normals;
  AutoHandle<Buffer> tangents;
  AutoHandle<Buffer> colors;
  AutoHandle<Buffer> uvs;
  AutoHandle<Buffer> indices;
  u32 num_free_vertices = NUM_VERTEX_POOL_VERTICES;
  u32 num_free_indices = NUM_VERTEX_POOL_INDICES;
};

using VertexPoolList = SmallVector<VertexPool, 1>;

auto create_vertex_pool(MeshAttributeFlags attributes) -> VertexPool;

struct MeshInstance {
  u32 mesh = 0;
  u32 material = 0;
  glm::mat4x3 matrix{1.0f};
};

} // namespace ren
