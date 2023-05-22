#pragma once
#include "cpp.h"

REN_NAMESPACE_BEGIN

struct GlobalData {
  float4x4 proj_view;
  float3 eye;
  uint num_dir_lights;
};

struct ColorPushConstants {
  uint matrix_index;
  uint material_index;
  uint64_t positions;
  uint64_t normals;
  uint64_t colors;
  uint64_t uvs;
};

static_assert(sizeof(ColorPushConstants) <= 128);

typedef float3x4 model_matrix_t;
typedef float3x3 normal_matrix_t;

constexpr uint GLOBAL_SET = 1;
constexpr uint FIRST_GLOBAL_SLOT = -1;
constexpr uint GLOBAL_DATA_SLOT = FIRST_GLOBAL_SLOT + 1;
constexpr uint MODEL_MATRICES_SLOT = GLOBAL_DATA_SLOT + 1;
constexpr uint NORMAL_MATRICES_SLOT = MODEL_MATRICES_SLOT + 1;
constexpr uint MATERIALS_SLOT = NORMAL_MATRICES_SLOT + 1;
constexpr uint DIR_LIGHTS_SLOT = MATERIALS_SLOT + 1;

REN_NAMESPACE_END
