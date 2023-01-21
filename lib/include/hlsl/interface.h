#pragma once
#include "cpp.h"

REN_NAMESPACE_BEGIN

struct GlobalData {
  float4x4 proj_view;
};

struct MaterialData {
  float3 color;
};

struct VertexData {
  uint matrix_index;
  uint64_t positions;
  uint64_t colors;
};

struct FragmentData {
  uint material_index;
};

struct PushConstants {
  VertexData vertex;
  FragmentData fragment;
};

typedef float3x4 model_matrix_t;

constexpr uint PERSISTENT_SET = 0;
constexpr uint MATERIALS_SLOT = 0;

constexpr uint GLOBAL_SET = 1;
constexpr uint GLOBAL_CB_SLOT = 0;
constexpr uint MATRICES_SLOT = 1;

constexpr uint MODEL_SET = 3;
constexpr uint POSITIONS_SLOT = 0;
constexpr uint COLORS_SLOT = 1;

REN_NAMESPACE_END
