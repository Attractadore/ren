#include "cpp.hlsl"

namespace ren {
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

constexpr uint GLOBAL_CB_SLOT = 0;
constexpr uint MATRICES_SLOT = 1;
constexpr uint MATERIALS_SLOT = 2;
} // namespace ren
