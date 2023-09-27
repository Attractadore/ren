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

struct MeshInstance {
  uint mesh;
  uint material;
};

GLSL_NAMESPACE_END

#endif // REN_GLSL_MESH_H
