#include "Lighting.glsl"
#include "Material.glsl"
#include "OpaquePass.glsl"
#include "Texture.glsl"

layout(location = V_POSITION) in vec3 v_position;

layout(location = V_NORMAL) in vec3 v_normal;
layout(location = V_TANGENT) in vec4 v_tangent;

layout(location = V_UV) in vec2 v_uv;

layout(location = V_COLOR) in vec4 v_color;

layout(location = V_MATERIAL) in flat uint v_material;

layout(location = 0) out vec4 f_color;

void main() {
  Material material = DEREF(pc.materials[v_material]);

  vec4 color = material.base_color;
  if (OPAQUE_FEATURE_VC) {
    color *= v_color;
  }

  if (OPAQUE_FEATURE_UV && !IS_NULL_DESC(material.base_color_texture)) {
    color *= texture(material.base_color_texture, v_uv);
  }

  float metallic = material.metallic;
  float roughness = material.roughness;
  if (OPAQUE_FEATURE_UV && !IS_NULL_DESC(material.metallic_roughness_texture)) {
    vec4 tex = texture(material.metallic_roughness_texture, v_uv);
    metallic *= tex.b;
    roughness *= tex.g;
  }

  vec3 normal = v_normal;
  if (OPAQUE_FEATURE_UV && OPAQUE_FEATURE_TS && !IS_NULL_DESC(material.normal_texture)) {
    vec3 tex = texture(material.normal_texture, v_uv).xyz;
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
    DirectionalLight light = DEREF(pc.directional_lights[i]);
    result.xyz += lighting(normal, light.origin, view, color.xyz, metallic, roughness, light.color * light.illuminance);
  }

  float exposure = texel_fetch(pc.exposure, ivec2(0), 0).r;
  result.xyz *= exposure;

  f_color = result;
}
