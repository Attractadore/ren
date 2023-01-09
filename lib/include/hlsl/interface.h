#pragma once
#include "cpp.h"

REN_NAMESPACE_BEGIN

struct GlobalData {
  float4x4 proj_view;
};

struct MaterialData {
  float3 color;
};

enum VertexFetch {
  Physical,
  Logical,
  Attribute,
};

template <VertexFetch VF> struct VertexDataTemplate { uint matrix_index; };

template <> struct VertexDataTemplate<VertexFetch::Physical> {
  uint matrix_index;
  uint64_t positions;
  uint64_t colors;
};

struct PixelData {
  uint material_index;
};

template <VertexFetch VF> struct PushConstantsTemplate {
  VertexDataTemplate<VF> vertex;
  PixelData pixel;
};

typedef float3x4 model_matrix_t;

#define PERSISTENT_SET 0
#define MATERIALS_SLOT 0

#define GLOBAL_SET 1
#define GLOBAL_CB_SLOT 0
#define MATRICES_SLOT 1

#define MODEL_SET 3
#define POSITIONS_SLOT 0
#define COLORS_SLOT 1

#define PUSH_CONSTANTS_REGISTER 666
#define PUSH_CONSTANTS_SPACE 666

REN_NAMESPACE_END
