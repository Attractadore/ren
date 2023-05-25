#ifndef REN_GLSL_COLOR_INTERFACE_H
#define REN_GLSL_COLOR_INTERFACE_H

#include "common.h"
#include "encode.h"

REN_NAMESPACE_BEGIN

struct GlobalData {
  mat4 proj_view;
  vec3 eye;
  uint num_dir_lights;
};

REN_BUFFER_REFERENCE(4) Positions { vec3 positions[1]; };
REN_BUFFER_REFERENCE(4) Colors { color_t colors[1]; };
REN_BUFFER_REFERENCE(4) Normals { normal_t normals[1]; };
REN_BUFFER_REFERENCE(4) UVs { vec2 uvs[1]; };

struct ColorPushConstants {
  REN_REFERENCE(Positions) positions_ptr;
  REN_REFERENCE(Colors) colors_ptr;
  REN_REFERENCE(Normals) normals_ptr;
  REN_REFERENCE(UVs) uvs_ptr;
  uint matrix_index;
  uint material_index;
};

static_assert(sizeof(ColorPushConstants) <= 128);

const uint GLOBAL_SET = 1;
const uint GLOBAL_DATA_SLOT = 0;
const uint MODEL_MATRICES_SLOT = GLOBAL_DATA_SLOT + 1;
const uint NORMAL_MATRICES_SLOT = MODEL_MATRICES_SLOT + 1;
const uint MATERIALS_SLOT = NORMAL_MATRICES_SLOT + 1;
const uint DIR_LIGHTS_SLOT = MATERIALS_SLOT + 1;

REN_NAMESPACE_END

#endif // REN_GLSL_COLOR_INTERFACE_H
