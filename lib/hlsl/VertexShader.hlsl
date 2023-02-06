#include "encode.h"
#include "interface.hlsl"

[[vk::binding(GLOBAL_CB_SLOT, GLOBAL_SET)]] ConstantBuffer<GlobalData> g_global;
[[vk::binding(MATRICES_SLOT, GLOBAL_SET)]] StructuredBuffer<model_matrix_t>
    g_matrices;

#if REFLECTION

float4 main() : SV_Position { return float4(0.0f, 0.0f, 0.0f, 0.0f); }

#else

[[vk::push_constant]] PushConstants g_pcs;

VS_OUT main(VS_IN vs_in) {
  VS_OUT vs_out;

  float3x4 model = g_matrices[g_pcs.vertex.matrix_index];
  float3 local_position = ptr_load<float3>(g_pcs.vertex.positions, vs_in.index);
  float3 world_position = mul(model, float4(local_position, 1.0f));
  vs_out.world_position = world_position;
  float4 position = mul(g_global.proj_view, float4(world_position, 1.0f));
  vs_out.position = position;

  float3 normal =
      decode_normal(ptr_load<normal_t>(g_pcs.vertex.normals, vs_in.index));
  // FIXME!!! Normals should be transformed
  vs_out.normal = normal;

#if ALBEDO_VERTEX
  float4 color = float4(
      decode_color(ptr_load<color_t>(g_pcs.vertex.colors, vs_in.index)), 1.0f);
  vs_out.color = color;
#endif

  return vs_out;
}

#endif
