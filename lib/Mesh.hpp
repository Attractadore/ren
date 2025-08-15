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

constexpr u32 MESH_PACKAGE_MAGIC = ('m' << 24) | ('n' << 16) | ('e' << 8) | 'r';
constexpr u32 MESH_PACKAGE_VERSION = 0;

struct MeshPackageHeader {
  u32 magic = MESH_PACKAGE_MAGIC;
  u32 version = MESH_PACKAGE_VERSION;
  u64 num_vertices = 0;
  u64 num_meshlets = 0;
  u64 num_indices = 0;
  u64 num_triangles = 0;
  u32 num_lods = 0;
  std::array<glsl::MeshLOD, glsl::MAX_NUM_LODS> lods = {};
  glsl::PositionBoundingBox bb = {};
  float scale = 0.0f;
  glsl::BoundingSquare uv_bs = {};
  u64 positions_offset = 0;
  u64 normals_offset = 0;
  u64 tangents_offset = 0;
  u64 uvs_offset = 0;
  u64 colors_offset = 0;
  u64 meshlets_offset = 0;
  u64 indices_offset = 0;
  u64 triangles_offset = 0;
};

class ResourceArena;

struct Buffer;

enum class MeshAttribute {
  UV = glsl::MESH_ATTRIBUTE_UV_BIT,
  Tangent = glsl::MESH_ATTRIBUTE_TANGENT_BIT,
  Color = glsl::MESH_ATTRIBUTE_COLOR_BIT,
};

} // namespace ren

REN_ENABLE_FLAGS(ren::MeshAttribute);

namespace ren {

using MeshAttributeFlags = Flags<MeshAttribute>;

struct Mesh {
  Handle<Buffer> positions;
  glsl::PositionBoundingBox bb = {};
  float scale = 0.0f;
  Handle<Buffer> normals;
  Handle<Buffer> tangents;
  Handle<Buffer> uvs;
  glsl::BoundingSquare uv_bs = {};
  Handle<Buffer> colors;
  u32 index_pool = -1;
  Handle<Buffer> meshlets;
  Handle<Buffer> indices;
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
