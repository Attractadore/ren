#ifndef REN_GLSL_MESH_H
#define REN_GLSL_MESH_H

#include "common.h"

GLSL_NAMESPACE_BEGIN

const uint MESH_ATTRIBUTE_UNUSED = -1;

struct Mesh {
  uint base_tangent_vertex;
  uint base_color_vertex;
  uint base_uv_vertex;
};

GLSL_BUFFER(4) Positions { vec3 position; };

struct MeshInstance {
  uint mesh;
  uint material;
};

GLSL_BUFFER(4) TransformMatrices { mat4x3 matrix; };

GLSL_NAMESPACE_END

#endif // REN_GLSL_MESH_H
