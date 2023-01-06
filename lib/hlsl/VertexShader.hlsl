#include "encode.hlsl"
#include "hlsl_interface.hlsl"

[[vk::binding(GLOBAL_CB_SLOT, SCENE_SET)]] cbuffer GlobalCB {
  GlobalData g_global;
};

[[vk::binding(MATRICES_SLOT, SCENE_SET)]] StructuredBuffer<float3x4> g_matrices;

PUSH_CONSTANTS(ModelData, g_model);

VS_OUT main(VS_IN vs_in) {
  VS_OUT vs_out;

  float3x4 model = g_matrices[g_model.matrix_index];
  float3 local_position = ptr_load<float3>(g_model.positions, vs_in.index);
  float3 world_position = mul(model, float4(local_position, 1.0f));
  vs_out.position = mul(g_global.proj_view, float4(world_position, 1.0f));

#if VERTEX_COLOR
  color_t encoded_color = ptr_load<color_t>(g_model.colors, vs_in.index);
  vs_out.color = decode_color(encoded_color);
#endif

  return vs_out;
}
