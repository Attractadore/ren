#include "Opaque.h"
#include "Lighting.h"
#include "Material.h"
#include "Math.h"
#include "SG.h"
#include "Texture.glsl"
#include "Transforms.h"

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
  vec3 f0 = F_schlick_f0(color.rgb, metallic);

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
    if (pc.num_brdf_sgs == 0) {
      result.xyz += lighting(normal, light.origin, view, albedo, f0, roughness, light.color * light.illuminance);
    } else {
      vec3 N = normal;
      vec3 V = view;
      vec3 L = light.origin;
      float NoV = dot(N, V);
      float NvV = acos_0_to_1_fast(NoV);
      float NoL = dot(N, L);

      vec3 kd = albedo * NoL / PI;
      vec3 ks = vec3(0.0f);

      vec3 B = normalize(cross(N, V));
      vec3 T = cross(B, N);
      mat3 TBN = mat3(T, B, N);

      float alpha2 = roughness * roughness;
      alpha2 = alpha2 * alpha2;
      float sh0 = 2.0f / alpha2;
      float shx0 = sh0 / 8;
      float shy0 = sh0 / (8 * NoV * NoV);

      uint base_sg = (pc.num_brdf_sgs - 1) * pc.num_brdf_sgs / 2;
      vec2 uv = sg_brdf_r_and_NvV_to_uv(roughness, NvV);
      for (uint i = 0; i < pc.num_brdf_sgs; ++i) {
        vec4 params = texture(pc.raw_sg_brdf_lut, vec3(uv, base_sg + i));
        float phi = params[0];
        float a = params[1];
        float lx = params[2];
        float ly = params[3];

        float cos_phi = cos(phi);
        float sin_phi = sin(phi);
        vec3 Z = TBN * vec3(cos_phi, 0.0f, sin_phi);
        vec3 Y = B;
        vec3 X = TBN * vec3(-sin_phi, 0.0f, cos_phi);
        vec3 H = normalize(Z + V);
        float NoH = dot(N, H);
        float VoH = dot(V, H);

        ASG asg;
        asg.z = Z;
        asg.x = X;
        asg.y = Y;
        asg.a = a * D_ggx(roughness, NoH);
        asg.lx = (lx * lx) * shx0;
        asg.ly = (ly * ly) * shy0;

        ks += F_schlick(f0, VoH) * eval_asg(asg, L);
      }
      ks = ks / (4.0f * NoV);

      vec3 E_p = light.color * light.illuminance;
      result.xyz += NoL > 0.0f ? E_p * (kd + ks) : vec3(0.0f);
    }

  }

  float ka = 1.0f;
  if (!IS_NULL_DESC(pc.ssao)) {
    vec2 uv = gl_FragCoord.xy * pc.inv_viewport;
    vec2 ba = texture_lod(pc.ssao, uv, 0).rg;
    float z = abs(1.0f / gl_FragCoord.w);
    ka = clamp(ba.x + ba.y * z, 0.0f, 1.0f);
  }
  ka = ka * occlusion;

#if 0
  if (!IS_NULL_DESC(pc.raw_env_map)) {
    result.xyz += env_lighting(normal, view, albedo, f0, roughness, pc.raw_env_map, ka, pc.raw_so_lut);
  } else {
    result.xyz += env_lighting(normal, view, albedo, f0, roughness, pc.env_luminance, ka, pc.raw_so_lut);
  }
#endif

  float exposure = texel_fetch(pc.exposure, ivec2(0), 0).r;
  result.xyz *= exposure;

  f_color = result;
}
