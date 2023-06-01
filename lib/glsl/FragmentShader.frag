#include "color_interface.glsl"
#include "interface.glsl"
#include "lighting.glsl"
#include "material.glsl"

TEXTURES;

PUSH_CONSTANTS { ColorConstants g_pcs; };

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

  ColorUB ub = g_pcs.ub_ptr;

  Material material = ub.materials_ptr[g_pcs.material_index].material;

  vec3 view_dir = normalize(ub.eye - position);

  color *= material.base_color;
  if (material.base_color_texture != 0) {
    color *= texture(g_textures2d[material.base_color_texture], uv);
  }

  float metallic = material.metallic;
  float roughness = material.roughness;

  vec4 result = vec4(0.0f, 0.0f, 0.0f, 1.0f);

  uint num_dir_lights = ub.num_dir_lights;
  for (int i = 0; i < num_dir_lights; ++i) {
    DirLight light = ub.directional_lights_ptr[i].light;
    vec3 light_dir = light.origin;
    vec3 light_color = light.color;
    float illuminance = light.illuminance;
    result.xyz += lighting(normal, light_dir, view_dir, color.xyz, metallic,
                           roughness, light_color * illuminance);
  }

  out_color = result * ub.exposure_ptr.exposure;
}
