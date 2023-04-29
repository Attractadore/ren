#include "interface.hlsl"
#include "lighting.hlsl"
#include "material.hlsl"

[[vk::binding(GLOBAL_DATA_SLOT, GLOBAL_SET)]] ConstantBuffer<GlobalData>
    g_global;
[[vk::binding(MATERIALS_SLOT, GLOBAL_SET)]] StructuredBuffer<Material>
    g_materials;
[[vk::binding(DIR_LIGHTS_SLOT, GLOBAL_SET)]] StructuredBuffer<DirLight>
    g_dir_lights;

#if REFLECTION

float4 main() : SV_Target { return float4(0.0f, 0.0f, 0.0f, 1.0f); }

#else

[[vk::push_constant]] PushConstants g_pcs;

float4 main(FS_IN fs_in) : SV_Target {
  Material material = g_materials[g_pcs.fragment.material_index];

  float3 normal = normalize(fs_in.normal);
  float3 view_dir = normalize(g_global.eye - fs_in.world_position);

  float4 color = material.base_color * fs_in.color;

  float metallic = material.metallic;
  float roughness = material.roughness;

  float4 result = float4(0.0f, 0.0f, 0.0f, 1.0f);

  uint num_dir_lights = g_global.num_dir_lights;
  for (int i = 0; i < num_dir_lights; ++i) {
    DirLight dir_light = g_dir_lights[i];
    float3 light_dir = dir_light.origin;
    float3 light_color = dir_light.color;
    float illuminance = dir_light.illuminance;
    result.xyz += lighting(normal, light_dir, view_dir, color.xyz, metallic,
                           roughness, light_color * illuminance);
  }

  // FIXME: placeholder Reinhard tonemapping
  result.xyz /= (1.0f + result.xyz);

  return result;
}

#endif
