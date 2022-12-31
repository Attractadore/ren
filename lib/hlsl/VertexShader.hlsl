#include "encode.hlsl"
#include "hlsl_interface.hlsl"

[[vk::binding(ren::GLOBAL_CB_SLOT)]] cbuffer GlobalCB {
  ren::GlobalData g_global;
};

[[vk::binding(ren::MATRICES_SLOT)]] StructuredBuffer<float3x4> g_matrices;

PUSH_CONSTANTS(ren::ModelData, g_model);

struct VS_IN {
  uint index : SV_VertexID;
};

struct VS_OUT {
  float4 position : SV_Position;
#if VERTEX_COLOR
  float3 color : COLOR0;
#endif
};

VS_OUT main(VS_IN vs_in) {
  VS_OUT vs_out;

  float3x4 model = g_matrices[g_model.matrix_index];
  float3 local_position = ren::ptr_load<float3>(g_model.positions, vs_in.index);
  float3 world_position = mul(model, float4(local_position, 1.0f));
  vs_out.position = mul(g_global.proj_view, float4(world_position, 1.0f));

#if VERTEX_COLOR
  ren::color_t encoded_color =
      ren::ptr_load<ren::color_t>(g_model.colors, vs_in.index);
  vs_out.color = ren::decode_color(encoded_color);
#endif

  return vs_out;
}
