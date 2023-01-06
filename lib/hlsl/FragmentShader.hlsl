#include "hlsl_interface.hlsl"

[[vk::binding(MATERIALS_SLOT, GLOBAL_SET)]] StructuredBuffer<MaterialData>
    g_materials;

PUSH_CONSTANTS(ModelData, g_model);

float4 main(PS_IN ps_in) : SV_Target {
  MaterialData material = g_materials[g_model.material_index];

  float3 color;
#if CONST_COLOR
  color = material.color;
#elif VERTEX_COLOR
  color = ps_in.color;
#endif

  return float4(color, 1.0f);
}
