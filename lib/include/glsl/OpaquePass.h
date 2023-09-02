#ifndef REN_GLSL_OPAQUE_PASS_H
#define REN_GLSL_OPAQUE_PASS_H

#include "Encode.h"
#include "Lighting.h"
#include "Material.h"
#include "common.h"

GLSL_NAMESPACE_BEGIN

GLSL_BUFFER(4) TransformMatrices { mat4x3 matrix; };

GLSL_BUFFER(4) NormalMatrices { mat3 matrix; };

GLSL_BUFFER(4) Materials { Material material; };

GLSL_BUFFER(4) DirectionalLights { DirLight light; };

GLSL_BUFFER(8) OpaqueUniformBuffer {
  GLSL_RESTRICT GLSL_READONLY GLSL_BUFFER_REFERENCE(TransformMatrices)
      transform_matrices;
  GLSL_RESTRICT GLSL_READONLY GLSL_BUFFER_REFERENCE(NormalMatrices)
      normal_matrices;
  GLSL_RESTRICT GLSL_READONLY GLSL_BUFFER_REFERENCE(Materials) materials;
  GLSL_RESTRICT GLSL_READONLY GLSL_BUFFER_REFERENCE(DirectionalLights)
      directional_lights;
  uint exposure_texture;
  mat4 pv;
  vec3 eye;
  uint num_directional_lights;
};

GLSL_BUFFER(4) Positions { vec3 position; };
GLSL_BUFFER(4) Colors { color_t color; };
GLSL_BUFFER(4) Normals { normal_t normal; };
GLSL_BUFFER(4) UVs { vec2 uv; };

#define GLSL_OPAQUE_CONSTANTS                                                  \
  {                                                                            \
    GLSL_RESTRICT GLSL_READONLY GLSL_BUFFER_REFERENCE(OpaqueUniformBuffer) ub; \
    GLSL_RESTRICT GLSL_READONLY GLSL_BUFFER_REFERENCE(Positions) positions;    \
    GLSL_RESTRICT GLSL_READONLY GLSL_BUFFER_REFERENCE(Colors) colors;          \
    GLSL_RESTRICT GLSL_READONLY GLSL_BUFFER_REFERENCE(Normals) normals;        \
    GLSL_RESTRICT GLSL_READONLY GLSL_BUFFER_REFERENCE(UVs) uvs;                \
    uint matrix;                                                               \
    uint material;                                                             \
  }

GLSL_NAMESPACE_END

#endif // REN_GLSL_OPAQUE_PASS_H
