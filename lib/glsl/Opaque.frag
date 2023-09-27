#include "Lighting.glsl"
#include "Material.glsl"
#include "OpaquePass.glsl"
#include "Textures.glsl"

TEXTURES;

PUSH_CONSTANTS GLSL_OPAQUE_CONSTANTS g_pcs;

IN_BLOCK FS_IN g_in;

OUT vec4 g_color;

void main() {
  vec3 position = g_in.position;
  vec3 normal = g_in.normal;
  vec4 color = g_in.color;
  vec2 uv = g_in.uv;
  uint material_index = g_in.material;

  Material material = g_pcs.ub.materials[material_index].material;

  vec3 view = normalize(g_pcs.ub.eye - position);

  color *= material.base_color;
  if (material.base_color_texture != 0) {
    color *= texture(g_textures2d[material.base_color_texture], uv);
  }

  float metallic = material.metallic;
  float roughness = material.roughness;
  if (material.metallic_roughness_texture != 0) {
    vec4 tex = texture(g_textures2d[material.metallic_roughness_texture], uv);
    metallic *= tex.b;
    roughness *= tex.g;
  }

  if (material.normal_texture != 0) {
    vec3 tangent = g_in.tangent;
    vec3 bitangent = g_in.bitangent;
    vec3 tex = texture(g_textures2d[material.normal_texture], uv).xyz;
    tex = 2.0f * tex - 1.0f;
    tex.xy *= material.normal_scale;
    normal = mat3(tangent, bitangent, normal) * tex;
  }
  normal = normalize(normal);

  vec4 result = vec4(0.0f, 0.0f, 0.0f, 1.0f);

  uint num_directional_lights = g_pcs.ub.num_directional_lights;
  for (int i = 0; i < num_directional_lights; ++i) {
    DirLight light = g_pcs.ub.directional_lights[i].light;
    result.xyz += lighting(normal, light.origin, view, color.xyz, metallic,
                           roughness, light.color * light.illuminance);
  }

  float exposure =
      imageLoad(g_rimages2d[g_pcs.ub.exposure_texture], ivec2(0)).r;
  result *= exposure;

  g_color = result;
}
