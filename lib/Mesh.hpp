#pragma once
#include "DrawSet.hpp"
#include "Material.hpp"
#include "core/Flags.hpp"
#include "core/GenIndex.hpp"
#include "core/StdDef.hpp"
#include "core/Vector.hpp"
#include "glsl/Mesh.h"

#include <array>
#include <glm/glm.hpp>

namespace ren {

class ResourceArena;

struct Buffer;

enum class MeshAttribute {
  UV = glsl::MESH_ATTRIBUTE_UV_BIT,
  Tangent = glsl::MESH_ATTRIBUTE_TANGENT_BIT,
  Color = glsl::MESH_ATTRIBUTE_COLOR_BIT,
};
REN_ENABLE_ENUM_FLAGS(MeshAttribute);

struct Mesh {
  Handle<Buffer> positions;
  glsl::PositionBoundingBox bb = {};
  glm::vec3 pos_enc_bb = glm::vec3(0.0f);
  Handle<Buffer> normals;
  Handle<Buffer> tangents;
  Handle<Buffer> uvs;
  glsl::BoundingSquare uv_bs = {};
  Handle<Buffer> colors;
  u32 index_pool = -1;
  Handle<Buffer> meshlets;
  Handle<Buffer> meshlet_indices;
  StaticVector<glsl::MeshLOD, glsl::MAX_NUM_LODS> lods;
};

struct IndexPool {
  Handle<Buffer> indices;
  u32 num_free_indices = glsl::INDEX_POOL_SIZE;
};

using IndexPoolList = SmallVector<IndexPool, 1>;

auto create_index_pool(ResourceArena &arena) -> IndexPool;

struct MeshInstance {
  Handle<Mesh> mesh;
  Handle<Material> material;
  DrawSetFlags draw_sets;
  std::array<DrawSetId, NUM_DRAW_SETS> draw_set_ids;
};

} // namespace ren
