#pragma once
#include "cpp.hlsl"

REN_NAMESPACE_BEGIN

struct GlobalData {
  float4x4 proj_view;
};

struct MaterialData {
  float3 color;
};

struct ModelData {
  uint matrix_index;
  uint material_index;
  uint64_t positions;
  uint64_t colors;
};

constexpr uint PERSISTENT_SET = 0;
constexpr uint MATERIALS_SLOT = 0;

constexpr uint GLOBAL_SET = 1;
constexpr uint GLOBAL_CB_SLOT = 0;
constexpr uint MATRICES_SLOT = 1;

REN_NAMESPACE_END
