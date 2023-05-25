#include "color_interface.glsl"
#include "interface.glsl"
#include "lighting.glsl"
#include "material.glsl"

SAMPLER2D(PERSISTENT_SET, SAMPLED_TEXTURES_SLOT) g_textures[NUM_SAMPLED_TEXTURES];

UBO(GLOBAL_SET, GLOBAL_DATA_SLOT) {
  GlobalData g_global;
};

SSBO(GLOBAL_SET, MATERIALS_SLOT, restrict readonly) {
  Material g_materials[];
};

SSBO(GLOBAL_SET, DIR_LIGHTS_SLOT, restrict readonly) {
  DirLight g_dir_lights[];
};

PUSH_CONSTANTS { ColorPushConstants g_pcs; };

IN(POSITION_LOCATION) vec3 in_position;
IN(COLOR_LOCATION) vec4 in_color;
IN(NORMAL_LOCATION) vec3 in_normal;
IN(UV_LOCATION) vec2 in_uv;

OUT(0) vec4 out_color;

void main() {
  vec3 position = in_position;
  vec4 color = in_color;
  vec3 normal = normalize(in_normal);
  vec2 uv = in_uv;

  Material material = g_materials[g_pcs.material_index];

  vec3 view_dir = normalize(g_global.eye - position);

  color *= material.base_color;
  if (material.base_color_texture != 0) {
    color *= texture(g_textures[material.base_color_texture], uv);
  }

  float metallic = material.metallic;
  float roughness = material.roughness;

  vec4 result = vec4(0.0f, 0.0f, 0.0f, 1.0f);

  uint num_dir_lights = g_global.num_dir_lights;
  for (int i = 0; i < num_dir_lights; ++i) {
    DirLight dir_light = g_dir_lights[i];
    vec3 light_dir = dir_light.origin;
    vec3 light_color = dir_light.color;
    float illuminance = dir_light.illuminance;
    result.xyz += lighting(normal, light_dir, view_dir, color.xyz, metallic,
                           roughness, light_color * illuminance);
  }

  out_color = result;
}
