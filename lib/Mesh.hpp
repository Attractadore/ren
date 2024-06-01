#pragma once
#include "Handle.hpp"
#include "Support/Flags.hpp"
#include "Support/StdDef.hpp"
#include "glsl/Mesh.h"

#include <glm/glm.hpp>

namespace ren {

class ResourceArena;

struct Buffer;

enum class MeshAttribute {
  UV = glsl::MESH_ATTRIBUTE_UV_BIT,
  Tangent = glsl::MESH_ATTRIBUTE_TANGENT_BIT,
  Color = glsl::MESH_ATTRIBUTE_COLOR_BIT,
};
ENABLE_FLAGS(MeshAttribute);

struct Meshlet {
  u32 base_vertex = 0;
  u32 base_index = 0;
};

struct Mesh {
  Handle<Buffer> positions;
  Handle<Buffer> normals;
  Handle<Buffer> tangents;
  Handle<Buffer> uvs;
  Handle<Buffer> colors;
  u32 index_pool = -1;
  glsl::PositionBoundingBox bb = {};
  glm::vec3 pos_enc_bb;
  glsl::BoundingSquare uv_bs = {};
  u32 base_index = 0;
  u32 num_indices = 0;
  StaticVector<glsl::MeshLOD, glsl::MAX_NUM_LODS> lods;
};

inline auto get_mesh_attribute_mask(const Mesh &mesh) -> u32 {
  uint mask = 0;
  if (mesh.tangents) {
    mask |= glsl::MESH_ATTRIBUTE_TANGENT_BIT;
  }
  if (mesh.uvs) {
    mask |= glsl::MESH_ATTRIBUTE_UV_BIT;
  }
  if (mesh.colors) {
    mask |= glsl::MESH_ATTRIBUTE_COLOR_BIT;
  }
  return mask;
};

struct IndexPool {
  Handle<Buffer> indices;
  u32 num_free_indices = glsl::INDEX_POOL_SIZE;
};

using IndexPoolList = SmallVector<IndexPool, 1>;

auto create_index_pool(ResourceArena &arena) -> IndexPool;

struct MeshInstance {
  u32 mesh = 0;
  u32 material = 0;
  glm::mat4x3 matrix{1.0f};
};

} // namespace ren
