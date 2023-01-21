#include "interface.hlsl"

[[vk::binding(MATERIALS_SLOT, PERSISTENT_SET)]] StructuredBuffer<MaterialData>
    g_materials;

#if REFLECTION

float4 main() : SV_Target { return float4(0.0f, 0.0f, 0.0f, 1.0f); }

#else

[[vk::push_constant]] PushConstants g_pcs;

float4 main(FS_IN fs_in) : SV_Target {
  MaterialData material = g_materials[g_pcs.fragment.material_index];

  float3 color = float3(0.0f, 0.0f, 0.0f);
#if ALBEDO_CONST
  color = material.color;
#elif ALBEDO_VERTEX
  color = fs_in.color;
#endif

  return float4(color, 1.0f);
}

#endif
