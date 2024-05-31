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
  MeshAttributeFlags attributes;
  u32 pool = -1;
  glsl::PositionBoundingBox bb = {};
  glm::vec3 pos_enc_bb;
  glsl::BoundingSquare uv_bs = {};
  u32 base_vertex = 0;
  u32 base_index = 0;
  u32 num_indices = 0;
  StaticVector<glsl::MeshLOD, glsl::MAX_NUM_LODS> lods;
};

struct VertexPool {
  Handle<Buffer> positions;
  Handle<Buffer> normals;
  Handle<Buffer> tangents;
  Handle<Buffer> colors;
  Handle<Buffer> uvs;
  Handle<Buffer> indices;
  u32 num_free_vertices = glsl::NUM_VERTEX_POOL_VERTICES;
  u32 num_free_indices = glsl::NUM_VERTEX_POOL_INDICES;
};

using VertexPoolList = SmallVector<VertexPool, 1>;

auto create_vertex_pool(ResourceArena &arena, MeshAttributeFlags attributes)
    -> VertexPool;

struct MeshInstance {
  u32 mesh = 0;
  u32 material = 0;
  glm::mat4x3 matrix{1.0f};
};

} // namespace ren
