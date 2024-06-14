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

GLSL_DEFINE_PTR_TYPE(Meshlet, 4);

const uint MAX_NUM_LODS = 8;

struct MeshLOD {
  uint base_meshlet;
  uint num_meshlets;
  uint base_index;
  uint num_indices;
};

struct Mesh {
  GLSL_PTR(Position) positions;
  GLSL_PTR(Normal) normals;
  GLSL_PTR(Tangent) tangents;
  GLSL_PTR(UV) uvs;
  GLSL_PTR(Color) colors;
  GLSL_PTR(Meshlet) meshlets;
  PositionBoundingBox bb;
  BoundingSquare uv_bs;
  uint index_pool;
  uint num_lods;
  MeshLOD lods[MAX_NUM_LODS];
};

GLSL_DEFINE_PTR_TYPE(Mesh, 8);

struct MeshInstance {
  uint mesh;
  uint material;
};

GLSL_DEFINE_PTR_TYPE(MeshInstance, 4);

GLSL_NAMESPACE_END

#endif // REN_GLSL_MESH_H
