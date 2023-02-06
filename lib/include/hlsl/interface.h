#pragma once
#include "cpp.h"

REN_NAMESPACE_BEGIN

struct GlobalData {
  float4x4 proj_view;
  float3 eye;
};

struct VertexData {
  uint matrix_index;
  uint64_t positions;
  uint64_t normals;
  uint64_t colors;
};

struct FragmentData {
  uint material_index;
};

struct PushConstants {
  VertexData vertex;
  FragmentData fragment;
};

static_assert(sizeof(PushConstants) <= 128);

typedef float3x4 model_matrix_t;

constexpr uint PERSISTENT_SET = 0;
constexpr uint MATERIALS_SLOT = 0;

constexpr uint GLOBAL_SET = 1;
constexpr uint GLOBAL_CB_SLOT = 0;
constexpr uint MATRICES_SLOT = 1;
constexpr uint LIGHTS_SLOT = 2;

REN_NAMESPACE_END
