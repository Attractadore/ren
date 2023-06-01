#ifndef REN_GLSL_COLOR_INTERFACE_H
#define REN_GLSL_COLOR_INTERFACE_H

#include "common.h"
#include "encode.h"
#include "exposure.h"
#include "lighting.h"
#include "material.h"

REN_NAMESPACE_BEGIN

REN_BUFFER_REFERENCE(4) TransformMatrices { mat4x3 matrix; };

REN_BUFFER_REFERENCE(4) NormalMatrices { mat3 matrix; };

REN_BUFFER_REFERENCE(4) Materials { Material material; };

REN_BUFFER_REFERENCE(4) DirectionalLights { DirLight light; };

REN_BUFFER_REFERENCE(4) ColorUB {
  REN_REFERENCE(TransformMatrices) transform_matrices_ptr;
  REN_REFERENCE(NormalMatrices) normal_matrices_ptr;
  REN_REFERENCE(Materials) materials_ptr;
  REN_REFERENCE(DirectionalLights) directional_lights_ptr;
  REN_REFERENCE(Exposure) exposure_ptr;
  mat4 proj_view;
  vec3 eye;
  uint num_dir_lights;
};

REN_BUFFER_REFERENCE(4) Positions { vec3 position; };
REN_BUFFER_REFERENCE(4) Colors { color_t color; };
REN_BUFFER_REFERENCE(4) Normals { normal_t normal; };
REN_BUFFER_REFERENCE(4) UVs { vec2 uv; };

struct ColorConstants {
  REN_REFERENCE(ColorUB) ub_ptr;
  REN_REFERENCE(Positions) positions_ptr;
  REN_REFERENCE(Colors) colors_ptr;
  REN_REFERENCE(Normals) normals_ptr;
  REN_REFERENCE(UVs) uvs_ptr;
  uint matrix_index;
  uint material_index;
};

static_assert(sizeof(ColorConstants) <= 128);

REN_NAMESPACE_END

#endif // REN_GLSL_COLOR_INTERFACE_H
