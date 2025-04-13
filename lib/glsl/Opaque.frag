#include "Lighting.h"
#include "Material.h"
#include "Opaque.h"
#include "Texture.glsl"

layout(location = A_POSITION) in vec3 a_position;

layout(location = A_NORMAL) in vec3 a_normal;
layout(location = A_TANGENT) in vec4 a_tangent;

layout(location = A_UV) in vec2 a_uv;

layout(location = A_COLOR) in vec4 a_color;

layout(location = A_MATERIAL) in flat uint a_material;

layout(location = 0) out vec4 f_color;

layout(origin_upper_left) in vec4 gl_FragCoord;

void main() {
  Material material = DEREF(pc.materials[a_material]);

  vec4 color = material.base_color;
  if (OPAQUE_FEATURE_VC) {
    color *= a_color;
  }

  if (OPAQUE_FEATURE_UV && !IS_NULL_DESC(material.base_color_texture)) {
    color *= texture(material.base_color_texture, a_uv);
  }

  float occlusion = 1.0f;
  float roughness = material.roughness;
  float metallic = material.metallic;
  if (OPAQUE_FEATURE_UV && !IS_NULL_DESC(material.orm_texture)) {
    vec4 orm = texture(material.orm_texture, a_uv);
    occlusion = 1.0f - material.occlusion_strength * (1.0f - orm.r);
    roughness *= orm.g;
    metallic *= orm.b;
  }

  vec3 albedo = mix(color.rgb, vec3(0.0f), metallic);
  vec3 f0 = fresnel_f0(color.rgb, metallic);

  vec3 normal = a_normal;
  if (OPAQUE_FEATURE_UV && OPAQUE_FEATURE_TS && !IS_NULL_DESC(material.normal_texture)) {
    vec3 tex = texture(material.normal_texture, a_uv).xyz;
    tex = 2.0f * tex - 1.0f;
    tex.xy *= material.normal_scale;
    vec3 tangent = a_tangent.xyz;
    float s = a_tangent.w;
    vec3 bitangent = s * cross(normal, tangent);
    normal = mat3(tangent, bitangent, normal) * tex;
  }
  normal = normalize(normal);

  vec4 result = vec4(0.0f, 0.0f, 0.0f, 1.0f);
  vec3 view = normalize(pc.eye - a_position);
  for (int i = 0; i < pc.num_directional_lights; ++i) {
    DirectionalLight light = DEREF(pc.directional_lights[i]);
    result.xyz += lighting(normal, light.origin, view, albedo, f0, roughness, light.color * light.illuminance);
  }

  vec3 ao = vec3(1.0f);
  if (!IS_NULL_DESC(pc.ssao)) {
    vec2 ao_uv = gl_FragCoord.xy / pc.viewport;
    ao = texture_lod(pc.ssao, ao_uv, 0.0f).rrr;
  }
  if (!IS_NULL_DESC(pc.raw_env_map)) {
    result.xyz += occlusion * env_lighting(normal, view, albedo, f0, roughness, pc.raw_env_map, ao, pc.raw_dhr_lut);
  } else {
    result.xyz += occlusion * env_lighting(normal, view, albedo, f0, roughness, pc.env_luminance, ao, pc.raw_dhr_lut);
  }

  float exposure = texel_fetch(pc.exposure, ivec2(0), 0).r;
  result.xyz *= exposure;

  f_color = result;
}
