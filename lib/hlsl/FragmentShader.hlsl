#include "hlsl_interface.hlsl"

[[vk::binding(ren::MATERIALS_SLOT)]] StructuredBuffer<ren::MaterialData>
    g_materials;

PUSH_CONSTANTS(ren::ModelData, g_model);

struct PS_IN {
#if VERTEX_COLOR
  float3 color : COLOR0;
#endif
};

float4 main(PS_IN ps_in) : SV_Target {
  ren::MaterialData material = g_materials[g_model.material_index];

  float3 color;
#if CONST_COLOR
  color = material.color;
#elif VERTEX_COLOR
  color = ps_in.color;
#endif

  return float4(color, 1.0f);
}
