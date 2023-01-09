#include "interface.hlsl"

StructuredBuffer<MaterialData> g_materials
    : TREGISTER(MATERIALS_SLOT, PERSISTENT_SET);

#if REFLECTION

float4 main() : SV_Target { return float4(0.0f, 0.0f, 0.0f, 1.0f); }

#else

PUSH_CONSTANTS(PushConstants, g_pcs);

float4 main(PS_IN ps_in) : SV_Target {
  MaterialData material = g_materials[g_pcs.pixel.material_index];

  float3 color = float3(0.0f, 0.0f, 0.0f);
#if ALBEDO_CONST
  color = material.color;
#elif ALBEDO_VERTEX
  color = ps_in.color;
#endif

  return float4(color, 1.0f);
}

#endif
