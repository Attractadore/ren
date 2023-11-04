#ifndef REN_GLSL_MESH_H
#define REN_GLSL_MESH_H

#include "common.h"

GLSL_NAMESPACE_BEGIN

GLSL_BUFFER(4) Positions { vec3 position; };

GLSL_BUFFER(4) Normals { vec3 normal; };

GLSL_BUFFER(4) Tangents { vec4 tangent; };

GLSL_BUFFER(4) UVs { vec2 uv; };

GLSL_BUFFER(4) Colors { vec4 color; };

struct MeshInstance {
  uint material;
};

GLSL_BUFFER(4) MeshInstances { MeshInstance mesh_instance; };

GLSL_BUFFER(4) TransformMatrices { mat4x3 matrix; };

GLSL_BUFFER(4) NormalMatrices { mat3 matrix; };

GLSL_NAMESPACE_END

#endif // REN_GLSL_MESH_H
