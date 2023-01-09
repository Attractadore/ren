#include "interface.hlsl"

[[vk::binding(MATERIALS_SLOT, PERSISTENT_SET)]] StructuredBuffer<MaterialData>
    g_materials;

PUSH_CONSTANTS(ModelData, g_model);

float4 main(PS_IN ps_in) : SV_Target {
  MaterialData material = g_materials[g_model.material_index];

  float3 color = float3(0.0f, 0.0f, 0.0f);
#if ALBEDO_CONST
  color = material.color;
#elif ALBEDO_VERTEX
  color = ps_in.color;
#endif

  return float4(color, 1.0f);
}
