#include "Lighting.glsl"
#include "Material.glsl"
#include "OpaquePass.glsl"
#include "Textures.glsl"

TEXTURES;

layout(location = V_POSITION) in vec3 v_position;

layout(location = V_NORMAL) in vec3 v_normal;
layout(location = V_TANGENT) in vec4 v_tangent;

layout(location = V_UV) in vec2 v_uv;

layout(location = V_COLOR) in vec4 v_color;

layout(location = V_MATERIAL) in flat uint v_material;

layout(location = 0) out vec4 f_color;

void main() {
  Material material = pc.materials[v_material].material;

  vec4 color = material.base_color;
  if (OPAQUE_FEATURE_VC) {
    color *= v_color;
  }

  if (OPAQUE_FEATURE_UV && material.base_color_texture != 0) {
    color *= texture(g_textures2d[material.base_color_texture], v_uv);
  }

  float metallic = material.metallic;
  float roughness = material.roughness;
  if (OPAQUE_FEATURE_UV && material.metallic_roughness_texture != 0) {
    vec4 tex = texture(g_textures2d[material.metallic_roughness_texture], v_uv);
    metallic *= tex.b;
    roughness *= tex.g;
  }

  vec3 normal = v_normal;
  if (OPAQUE_FEATURE_UV && OPAQUE_FEATURE_TS && material.normal_texture != 0) {
    vec3 tex = texture(g_textures2d[material.normal_texture], v_uv).xyz;
    tex = 2.0f * tex - 1.0f;
    tex.xy *= material.normal_scale;
    vec3 tangent = v_tangent.xyz;
    float s = v_tangent.w;
    vec3 bitangent = s * cross(normal, tangent);
    normal = mat3(tangent, bitangent, normal) * tex;
  }
  normal = normalize(normal);

  vec4 result = vec4(0.0f, 0.0f, 0.0f, 1.0f);
  vec3 view = normalize(pc.eye - v_position);
  for (int i = 0; i < pc.num_directional_lights; ++i) {
    DirLight light = pc.directional_lights[i].light;
    result.xyz += lighting(normal, light.origin, view, color.xyz, metallic, roughness, light.color * light.illuminance);
  }

  float exposure = imageLoad(g_rimages2d[pc.exposure_texture], ivec2(0)).r;
  result.xyz *= exposure;

  f_color = result;
}
