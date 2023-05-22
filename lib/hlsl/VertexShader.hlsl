#include "color_interface.hlsl"
#include "encode.h"
#include "memory.hlsl"

[[vk::binding(GLOBAL_DATA_SLOT, GLOBAL_SET)]] ConstantBuffer<GlobalData> g_global;
[[vk::binding(MODEL_MATRICES_SLOT,GLOBAL_SET)]] StructuredBuffer<model_matrix_t> g_model_matrices;
[[vk::binding(NORMAL_MATRICES_SLOT, GLOBAL_SET)]] StructuredBuffer<normal_matrix_t> g_normal_matrices;

[[vk::push_constant]] ColorPushConstants g_pcs;

#if REFLECTION

float4 main() : SV_Position { return float4(0.0f, 0.0f, 0.0f, 1.0f); }

#else

VS_OUT main(VS_IN vs_in) {
  VS_OUT vs_out;

  uint matrix_index = g_pcs.matrix_index;

  float3x4 model_mat = g_model_matrices[matrix_index];
  float3x3 normal_mat = g_normal_matrices[matrix_index];
  float4x4 proj_view_mat = g_global.proj_view;
  uint64_t positions_ptr = g_pcs.positions;
  uint64_t colors_ptr = g_pcs.colors;
  uint64_t normals_ptr = g_pcs.normals;
  uint64_t uvs_ptr = g_pcs.uvs;

  float3 local_position = ptr_load<float3>(positions_ptr, vs_in.index);
  float3 world_position = mul(model_mat, float4(local_position, 1.0f));
  vs_out.world_position = world_position;

  float4 position = mul(proj_view_mat, float4(world_position, 1.0f));
  vs_out.position = position;

  float4 color = 1.0f;
  if (colors_ptr) {
    color.xyz = decode_color(ptr_load<color_t>(colors_ptr, vs_in.index));
  }
  vs_out.color = color;

  float3 normal = decode_normal(ptr_load<normal_t>(normals_ptr, vs_in.index));
  normal = mul(normal_mat, normal);
  vs_out.normal = normal;

  float2 uv = 0.0f;
  if (uvs_ptr) {
    uv = ptr_load<float2>(uvs_ptr, vs_in.index);
  }
  vs_out.uv = uv;

  return vs_out;
}

#endif
