#include "Lighting.glsl"
#include "Material.glsl"
#include "OpaquePass.glsl"
#include "Textures.glsl"

TEXTURES;

PUSH_CONSTANTS { OpaqueConstants g_pcs; };

IN_BLOCK FS_IN g_in;

OUT vec4 g_color;

void main() {
  vec3 position = g_in.position;
  vec4 color = g_in.color;
  vec3 normal = normalize(g_in.normal);
  vec2 uv = g_in.uv;

  restrict readonly OpaqueUniformBuffer ub = g_pcs.ub;

  Material material = ub.materials[g_pcs.material].material;

  vec3 view_dir = normalize(ub.eye - position);

  color *= material.base_color;
  if (material.base_color_texture != 0) {
    color *= texture(g_textures2d[material.base_color_texture], uv);
  }

  float metallic = material.metallic;
  float roughness = material.roughness;

  vec4 result = vec4(0.0f, 0.0f, 0.0f, 1.0f);

  uint num_dir_lights = ub.num_directional_lights;
  for (int i = 0; i < num_dir_lights; ++i) {
    DirLight light = ub.directional_lights[i].light;
    vec3 light_dir = light.origin;
    vec3 light_color = light.color;
    float illuminance = light.illuminance;
    result.xyz += lighting(normal, light_dir, view_dir, color.xyz, metallic,
                           roughness, light_color * illuminance);
  }

  restrict readonly Exposure exposure = ub.exposure;
  result *= exposure.exposure;

  g_color = result;
}
