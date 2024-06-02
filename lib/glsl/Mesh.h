#ifndef REN_GLSL_MESH_H
#define REN_GLSL_MESH_H

#include "Common.h"
#include "Vertex.h"

GLSL_NAMESPACE_BEGIN

const uint MESH_ATTRIBUTE_UV_BIT = 1 << 0;
const uint MESH_ATTRIBUTE_TANGENT_BIT = 1 << 1;
const uint MESH_ATTRIBUTE_COLOR_BIT = 1 << 2;

const uint NUM_MESH_ATTRIBUTE_FLAGS =
    (MESH_ATTRIBUTE_UV_BIT | MESH_ATTRIBUTE_TANGENT_BIT |
     MESH_ATTRIBUTE_COLOR_BIT) +
    1;

const uint MAX_NUM_INDEX_POOL_BITS = 8;
const uint MAX_NUM_INDEX_POOLS = 1 << MAX_NUM_INDEX_POOL_BITS;

const uint INDEX_POOL_SIZE = 1 << 24;

const uint NUM_MESHLET_VERTICES = 64;
const uint NUM_MESHLET_TRIANGLES = 124;

struct Meshlet {
  uint base_index;
  uint num_indices;
  uint base_triangle;
  uint num_triangles;
};

GLSL_REF_TYPE(4) MeshletRef { Meshlet meshlet; };

const uint MAX_NUM_LODS = 8;

struct MeshLOD {
  uint base_meshlet;
  uint num_meshlets;
  uint base_index;
  uint num_indices;
};

struct Mesh {
  GLSL_REF(PositionRef) positions;
  GLSL_REF(NormalRef) normals;
  GLSL_REF(TangentRef) tangents;
  GLSL_REF(UVRef) uvs;
  GLSL_REF(ColorRef) colors;
  GLSL_REF(MeshletRef) meshlets;
  PositionBoundingBox bb;
  BoundingSquare uv_bs;
  uint index_pool;
  uint num_lods;
  MeshLOD lods[MAX_NUM_LODS];
};

GLSL_REF_TYPE(8) MeshRef { Mesh mesh; };

inline uint get_mesh_attribute_mask(Mesh mesh) {
  uint mask = 0;
  if (!GLSL_IS_NULL(mesh.tangents)) {
    mask |= MESH_ATTRIBUTE_TANGENT_BIT;
  }
  if (!GLSL_IS_NULL(mesh.uvs)) {
    mask |= MESH_ATTRIBUTE_UV_BIT;
  }
  if (!GLSL_IS_NULL(mesh.colors)) {
    mask |= MESH_ATTRIBUTE_COLOR_BIT;
  }
  return mask;
}

struct MeshInstance {
  uint mesh;
  uint material;
};

GLSL_REF_TYPE(4) MeshInstanceRef { MeshInstance mesh_instance; };

GLSL_REF_TYPE(4) TransformMatrixRef { mat4x3 matrix; };

GLSL_REF_TYPE(4) NormalMatrixRef { mat3 matrix; };

GLSL_NAMESPACE_END

#endif // REN_GLSL_MESH_H
