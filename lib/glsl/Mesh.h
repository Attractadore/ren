#ifndef REN_GLSL_MESH_H
#define REN_GLSL_MESH_H

#include "Vertex.h"
#include "common.h"

GLSL_NAMESPACE_BEGIN

GLSL_BUFFER(2) Positions { Position position; };

GLSL_BUFFER(2) Normals { Normal normal; };

GLSL_BUFFER(2) Tangents { Tangent tangent; };

GLSL_BUFFER(4) UVs { UV uv; };

GLSL_BUFFER(4) Colors { Color color; };

const uint MESH_ATTRIBUTE_UV_BIT = 1 << 0;
const uint MESH_ATTRIBUTE_TANGENT_BIT = 1 << 1;
const uint MESH_ATTRIBUTE_COLOR_BIT = 1 << 2;

const uint NUM_MESH_ATTRIBUTE_FLAGS =
    (MESH_ATTRIBUTE_UV_BIT | MESH_ATTRIBUTE_TANGENT_BIT |
     MESH_ATTRIBUTE_COLOR_BIT) +
    1;

const uint MAX_NUM_VERTEX_POOLS = 256;

const uint NUM_VERTEX_POOL_INDICES = 1 << 24;
const uint NUM_VERTEX_POOL_VERTICES = NUM_VERTEX_POOL_INDICES / 7;

const uint MAX_NUM_LODS = 8;

struct MeshLOD {
  uint base_index;
  uint num_indices;
};

struct MeshCullData {
  uint8_t attribute_mask;
  uint8_t pool;
  PositionBoundingBox bb;
  uint base_vertex;
  uint num_lods;
  MeshLOD lods[MAX_NUM_LODS];
};

struct MeshInstanceCullData {
  uint mesh;
};

struct MeshInstanceDrawData {
  BoundingSquare uv_bs;
  uint material;
};

GLSL_BUFFER(4) CullMeshes { MeshCullData data; };

GLSL_BUFFER(4) CullMeshInstances { MeshInstanceCullData data; };

GLSL_BUFFER(4) DrawMeshInstances { MeshInstanceDrawData data; };

GLSL_BUFFER(4) TransformMatrices { mat4x3 matrix; };

GLSL_BUFFER(4) NormalMatrices { mat3 matrix; };

GLSL_NAMESPACE_END

#endif // REN_GLSL_MESH_H
