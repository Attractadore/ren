#pragma once
#include "BRDF.h"
#include "SG.h"
#include "Std.h"
#include "Texture.h"
#if GL_core_profile
#include "Math.h"
#include "Texture.glsl"
#endif

GLSL_NAMESPACE_BEGIN

struct DirectionalLight {
  vec3 color;
  float illuminance;
  vec3 origin;
};

GLSL_DEFINE_PTR_TYPE(DirectionalLight, 4);

inline vec3 lighting(vec3 N, vec3 L, vec3 V, vec3 albedo, vec3 f0,
                     float roughness, vec3 E_p) {
  float NoV = dot(N, V);
  float NoL = dot(N, L);
  vec3 H = normalize(V + L);
  float NoH = dot(N, H);
  float VoH = dot(V, H);

  vec3 kd = albedo * NoL / PI;

  vec3 F = F_schlick(f0, VoH);
  float G = G_smith(roughness, NoL, NoV);
  float D = D_ggx(roughness, NoH);
  vec3 ks = F * (G * D / (4.0f * NoV));

  return NoL > 0.0f ? E_p * (kd + ks) : vec3(0.0f);
}

inline vec3 ka_with_interreflection(float ka, vec3 albedo) {
  return ka * (1.0f - albedo * (1.0f - ka));
}

struct Environment {
  GLSL_UNQUALIFIED_PTR(SG3) sgs;
  uint num_sgs;
  SampledTextureCube map;
};

#if GL_core_profile

inline vec3 specular_occlusion(SampledTexture3D lut, vec3 f0, float roughness,
                               float nv, float ka) {
  float cosa = sqrt(1.0f - ka);
  vec2 ab = texture_lod(lut, vec3(roughness, nv, cosa), 0).xy;
  return f0 * ab.x + ab.y;
}

inline vec3 env_lighting(vec3 n, vec3 v, vec3 albedo, vec3 f0, float roughness,
                         vec3 luminance, float ka, SampledTexture3D so_lut) {
  vec3 kd = ka_with_interreflection(ka, albedo) * albedo;
  vec3 ks = specular_occlusion(so_lut, f0, roughness, dot(n, v), ka);
  return (kd + ks) * luminance;
}

inline vec3 env_lighting(vec3 n, vec3 v, vec3 albedo, vec3 f0, float roughness,
                         SampledTextureCube env_map, float ka,
                         SampledTexture3D so_lut) {
  vec3 kd = ka_with_interreflection(ka, albedo) * albedo;
  float nv = dot(n, v);
  vec3 ks = specular_occlusion(so_lut, f0, roughness, nv, ka);
  vec3 r = 2 * nv * n - v;
  int num_mips = texture_query_levels(env_map);
  float dlod = num_mips - 1;
  float slod = roughness * (num_mips - 2);
  return kd * texture_lod(env_map, n, dlod).rgb +
         ks * texture_lod(env_map, r, slod).rgb;
}

inline vec3 env_lighting(vec3 N, vec3 V, vec3 albedo, vec3 f0, float roughness,
                         Environment env, SampledTexture2DArray sg_brdf_lut) {
  vec3 ks_analytical = vec3(0.0f);
  vec3 ks_convolved = vec3(0.0f);
  vec3 ks_rough = vec3(0.0f);

  float NoV = dot(N, V);
  float NvV = acos_0_to_1_fast(NoV);

  vec3 B = normalize(cross(N, V));
  vec3 T = cross(B, N);
  mat3 TBN = mat3(T, B, N);

  float shx0 = roughness_to_asg_sharpness(roughness);
  float shy0 = roughness_to_asg_sharpness(roughness, NoV);

  if (roughness < ANALYTICAL_SG_BRDF_ROUGHNESS_HIGH) {
    vec3 R = 2.0f * NoV * N - V;

    vec3 Z = R;
    vec3 Y = B;
    vec3 X = cross(Y, Z);

    vec3 F = F_schlick(f0, NoV);
    float G = G_smith(roughness, NoV, NoV);
    float D = D_ggx(roughness, 1.0f);
    float Q = 4.0f * NoV;

    ASG asg;
    asg.z = Z;
    asg.x = X;
    asg.y = Y;
    asg.a = G * D / Q;
    asg.lx = shx0;
    asg.ly = shy0;

    ks_analytical = F * sample_convolved_asg(asg, env.map);
  }

  if (roughness > ANALYTICAL_SG_BRDF_ROUGHNESS_LOW &&
      roughness < CONVOLVED_SG_BRDF_ROUGHNESS_HIGH) {
    const uint NUM_BRDF_SGS = 2;
    const uint BASE_SG = (NUM_BRDF_SGS - 1) * NUM_BRDF_SGS / 2;
    vec2 uv = sg_brdf_r_and_NvV_to_uv(roughness, NvV);
    vec3 FGD = vec3(0.0f);
    for (uint i = 0; i < NUM_BRDF_SGS; ++i) {
      vec4 params = texture(sg_brdf_lut, vec3(uv, BASE_SG + i));
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

      FGD += F_schlick(f0, VoH) * sample_convolved_asg(asg, env.map);
    }
    float Q = 4.0f * NoV;
    ks_convolved = FGD / Q;
  }

  if (roughness > CONVOLVED_SG_BRDF_ROUGHNESS_LOW) {
    const uint NUM_BRDF_SGS = 4;
    const uint BASE_SG = (NUM_BRDF_SGS - 1) * NUM_BRDF_SGS / 2;
    vec2 uv = sg_brdf_r_and_NvV_to_uv(roughness, NvV);
    vec3 FGD = vec3(0.0f);
    for (uint i = 0; i < NUM_BRDF_SGS; ++i) {
      vec4 params = texture(sg_brdf_lut, vec3(uv, BASE_SG + i));
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

      vec3 conv = vec3(0.0f);
      for (uint s = 0; s < env.num_sgs; ++s) {
        conv += convolve_asg_with_sg(asg, DEREF(env.sgs[s]));
      }

      FGD += F_schlick(f0, VoH) * conv;
    }
    float Q = 4.0f * NoV;
    ks_rough = FGD / Q;
  }

  vec3 ks = mix(ks_analytical, ks_convolved,
                smoothstep(ANALYTICAL_SG_BRDF_ROUGHNESS_LOW,
                           ANALYTICAL_SG_BRDF_ROUGHNESS_HIGH, roughness));
  ks = mix(ks, ks_rough,
           smoothstep(CONVOLVED_SG_BRDF_ROUGHNESS_LOW,
                      CONVOLVED_SG_BRDF_ROUGHNESS_HIGH, roughness));

  return ks;
}

#endif // GL_core_profile

GLSL_NAMESPACE_END
