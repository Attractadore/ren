#pragma once
#include "BRDF.h"
#include "DevicePtr.h"
#include "Std.h"
#include "Texture.h"
#if GL_core_profile
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

#endif // GL_core_profile

GLSL_NAMESPACE_END
