#include "encode.h"
#include "interface.hlsl"

ConstantBuffer<GlobalData> g_global : BREGISTER(GLOBAL_CB_SLOT, GLOBAL_SET);
StructuredBuffer<model_matrix_t> g_matrices
    : TREGISTER(MATRICES_SLOT, GLOBAL_SET);

#if REFLECTION

float4 main() : SV_Position { return float4(0.0f, 0.0f, 0.0f, 0.0f); }

#else

PUSH_CONSTANTS(PushConstants, g_pcs);

#if VERTEX_FETCH_LOGICAL
StructuredBuffer<float3> positions : TREGISTER(POSITIONS_SLOT, MODEL_SET);
#if ALBEDO_VERTEX
StructuredBuffer<color_t> colors : TREGISTER(COLORS_SLOT, MODEL_SET);
#endif
#endif

VS_OUT main(VS_IN vs_in) {
  VS_OUT vs_out;

  float3x4 model = g_matrices[g_pcs.vertex.matrix_index];
  float3 local_position =
#if VERTEX_FETCH_PHYSICAL
      ptr_load<float3>(g_pcs.vertex.positions, vs_in.index);
#elif VERTEX_FETCH_LOGICAL
      positions[vs_in.index];
#elif VERTEX_FETCH_ATTRIBUTE
      vs_in.position;
#endif
  float3 world_position = mul(model, float4(local_position, 1.0f));
  vs_out.position = mul(g_global.proj_view, float4(world_position, 1.0f));

#if ALBEDO_VERTEX
  float3 color =
#if VERTEX_FETCH_PHYSICAL
      decode_color(ptr_load<color_t>(g_pcs.vertex.colors, vs_in.index));
#elif VERTEX_FETCH_LOGICAL
      decode_color(colors[vs_in.index]);
#elif VERTEX_FETCH_ATTRIBUTE
      vs_in.color;
#endif
  vs_out.color = color;
#endif

  return vs_out;
}

#endif
