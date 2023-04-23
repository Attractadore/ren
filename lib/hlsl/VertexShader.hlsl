#include "encode.h"
#include "interface.hlsl"

[[vk::binding(GLOBAL_CB_SLOT, GLOBAL_SET)]] ConstantBuffer<GlobalData> g_global;
[[vk::binding(MODEL_MATRICES_SLOT,
              GLOBAL_SET)]] StructuredBuffer<model_matrix_t>
    g_model_matrices;
[[vk::binding(NORMAL_MATRICES_SLOT,
              GLOBAL_SET)]] StructuredBuffer<normal_matrix_t>
    g_normal_matrices;

#if REFLECTION

float4 main() : SV_Position { return float4(0.0f, 0.0f, 0.0f, 1.0f); }

#else

[[vk::push_constant]] PushConstants g_pcs;

VS_OUT main(VS_IN vs_in) {
  VS_OUT vs_out;

  uint matrix_index = g_pcs.vertex.matrix_index;

  float3x4 model_mat = g_model_matrices[matrix_index];
  float3x3 normal_mat = g_normal_matrices[matrix_index];
  float4x4 proj_view_mat = g_global.proj_view;
  uint64_t positions_ptr = g_pcs.vertex.positions;
  uint64_t colors_ptr = g_pcs.vertex.colors;
  uint64_t normals_ptr = g_pcs.vertex.normals;

  float3 local_position = ptr_load<float3>(positions_ptr, vs_in.index);
  float3 world_position = mul(model_mat, float4(local_position, 1.0f));
  vs_out.world_position = world_position;

  float4 position = mul(proj_view_mat, float4(world_position, 1.0f));
  vs_out.position = position;

  float4 color = 1.0f;
  if (colors_ptr != 0) {
    color.xyz = decode_color(ptr_load<color_t>(colors_ptr, vs_in.index));
  }
  vs_out.color = color;

  float3 normal = decode_normal(ptr_load<normal_t>(normals_ptr, vs_in.index));
  normal = mul(normal_mat, normal);
  vs_out.normal = normal;

  return vs_out;
}

#endif
