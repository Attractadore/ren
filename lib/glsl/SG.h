#pragma once
#include "Std.h"
#include "Transforms.h"
#if GL_core_profile
#include "Texture.glsl"
#endif
#include "DevicePtr.h"

GLSL_NAMESPACE_BEGIN

struct SG I_DIFFERENTIABLE {
  vec3 z;
  float a;
  float l;
};

GLSL_DEFINE_PTR_TYPE(SG, 4);

DIFFERENTIABLE
inline float eval_sg(SG sg, vec3 V) {
  return sg.a * exp(sg.l * (dot(sg.z, V) - 1.0f));
}

struct ASG I_DIFFERENTIABLE {
  vec3 z;
  vec3 x;
  vec3 y;
  float a;
  float lx;
  float ly;
};

GLSL_DEFINE_PTR_TYPE(ASG, 4);

DIFFERENTIABLE
inline float eval_asg(ASG asg, vec3 V) {
  float VoX = dot(asg.x, V);
  float VoY = dot(asg.y, V);
  return asg.a * max(dot(asg.z, V), 0.0f) *
         exp(-asg.lx * VoX * VoX - asg.ly * VoY * VoY);
}

inline float asg_F(float a) {
  float a2 = a * a;
  float a3 = a * a2;
  float a4 = a2 * a2;
  vec4 va3 = vec4(a3, a2, a, 1.0f);
  const vec4 p = vec4(0.7846f, 3.185f, 8.775f, 51.51f);
  const vec4 q = vec4(0.2126f, 0.808f, 1.523f, 1.305f);
  return sqrt(dot(p, va3) / (a4 + dot(q, va3)));
}

inline float integrate_asg(ASG asg) {
  float l = max(asg.lx, asg.ly);
  float u = min(asg.lx, asg.ly);
  float v = l - u;
  return asg.a * (PI / sqrt(l * u) -
                  0.5f * exp(-u) / l * (asg_F(v) + v / u * asg_F(v + v / u)));
}

inline ASG normalize_asg(ASG asg) {
  asg.a = asg.a / integrate_asg(asg);
  return asg;
}

static const uint NUM_SG_BRDF_SAMPLE_POINTS = 2048;
static const uint SG_BRDF_ROUGHNESS_SIZE = 32;
static const uint SG_BRDF_NvV_SIZE = 32;
static const uint MAX_SG_BRDF_SIZE = 4;

static const uint NUM_SG_BRDF_LAYERS =
    (MAX_SG_BRDF_SIZE + 1) * MAX_SG_BRDF_SIZE / 2;
static const uint NUM_SG_BRDF_PARAMS = 4;
static const uint MAX_SG_BRDF_PARAMS = MAX_SG_BRDF_SIZE * NUM_SG_BRDF_PARAMS;

// 0.15 - 0.20 -- blend between 1 analytically fit ASG and 2 ASGs from LUT.
// 0.30 - 0.35 -- blend between 2 ASGs from LUT convolved with a cube map and 4
// ASGs from LUT convolved with an SG mixture.
static const float ANALYTICAL_SG_BRDF_ROUGHNESS_LOW = 0.15f;
static const float ANALYTICAL_SG_BRDF_ROUGHNESS_HIGH = 0.20f;
static const float CONVOLVED_SG_BRDF_ROUGHNESS_LOW = 0.30f;
static const float CONVOLVED_SG_BRDF_ROUGHNESS_HIGH = 0.35f;
static const float MIN_CONVOLVED_SG_CUBE_MAP_ROUGHNESS = 1.0f;

inline vec2 sg_brdf_r_and_NvV_to_uv(float roughness, float phi) {
  float uv_x = (roughness - ANALYTICAL_SG_BRDF_ROUGHNESS_LOW) /
               (1.0f - ANALYTICAL_SG_BRDF_ROUGHNESS_LOW);
  float phi_norm = phi / (0.5f * PI);
  float uv_y = mix(0.5f / SG_BRDF_NvV_SIZE, 1.0f, phi_norm);
  return vec2(uv_x, uv_y);
}

inline float sg_brdf_uv_to_r(float uv_x) {
  return mix(ANALYTICAL_SG_BRDF_ROUGHNESS_LOW, 1.0f, uv_x);
}

inline float sg_brdf_uv_to_NvV(float uv_y) {
  float phi_norm = (uv_y * SG_BRDF_NvV_SIZE - 0.5f) / (SG_BRDF_NvV_SIZE - 0.5f);
  return 0.5f * PI * clamp(phi_norm, 0.0f, 1.0f);
}

inline vec2 sg_brdf_uv_to_r_and_NvV(vec2 uv) {
  return vec2(sg_brdf_uv_to_r(uv.x), sg_brdf_uv_to_NvV(uv.y));
}

inline vec3 importance_sample_sg_hemisphere(vec2 Xi, float sharpness) {
  float phi = Xi.x * TWO_PI;

  const float APPROX_LOW = 10.0f;
  const float APPROX_HIGH = 11.0f;

  float z_exact = log((exp(sharpness) - 1.0f) * Xi.y + 1.0f) / sharpness;
  float z_approx = (sharpness + log(Xi.y)) / sharpness;
  float z = isinf(z_exact)
                ? z_approx
                : mix(z_exact, z_approx,
                      smoothstep(APPROX_LOW, APPROX_HIGH, sharpness));

  float r = sqrt(1.0f - z * z);

  return vec3(r * cos(phi), r * sin(phi), z);
}

inline vec3 importance_sample_sg_hemisphere(vec2 Xi, float sharpness, vec3 Z) {
  return make_orthonormal_basis(Z) *
         importance_sample_sg_hemisphere(Xi, sharpness);
}

inline float roughness_to_asg_sharpness(float roughness, float NoV) {
  float alpha = roughness * roughness;
  return 1.0f / (4.0f * alpha * alpha * NoV * NoV);
}

inline float roughness_to_asg_sharpness(float roughness) {
  return roughness_to_asg_sharpness(roughness, 1.0f);
}

inline float asg_sharpness_to_roughness(float sh) {
  return sqrt(sqrt(1.0f / (4.0f * sh)));
}

inline void ellipse_transform_derivatives(GLSL_INOUT(vec2) X,
                                          GLSL_INOUT(vec2) Y) {
  float a = X.y * X.y + Y.y * Y.y;
  float b = -2.0f * (X.x * X.y + Y.x * Y.y);
  float c = X.x * X.x + Y.x * Y.x;
  float f = (X.x * Y.y - Y.x * X.y) * (X.x * Y.y - Y.x * X.y);
  float p = a - c;
  float q = a + c;
  float t = sqrt(p * p + b * b);
  X.x = sqrt(f * (t + p) / (t * (q + t)));
  X.y = sqrt(f * (t - p) / (t * (q + t))) * sign(b);
  Y.x = sqrt(f * (t - p) / (t * (q - t))) * -sign(b);
  Y.y = sqrt(f * (t + p) / (t * (q - t)));
}

inline float calculate_texture_grad_lod(float size, vec3 P, vec3 dPdx,
                                        vec3 dPdy) {
  vec3 axis = abs(P);
  float major_axis = max(axis.x, max(axis.y, axis.z));

  if (major_axis == P.z) {
    P = vec3(P.x, -P.y, P.z);
    dPdx = vec3(dPdx.x, -dPdx.y, dPdx.z);
    dPdy = vec3(dPdy.x, -dPdy.y, dPdy.z);
  } else if (major_axis == -P.z) {
    P = vec3(-P.x, -P.y, P.z);
    dPdx = vec3(-dPdx.x, -dPdx.y, dPdx.z);
    dPdy = vec3(-dPdy.x, -dPdy.y, dPdy.z);
  } else if (major_axis == P.y) {
    P = vec3(P.x, P.z, P.y);
    dPdx = vec3(dPdx.x, dPdx.z, dPdx.y);
    dPdy = vec3(dPdy.x, dPdy.z, dPdy.y);
  } else if (major_axis == -P.y) {
    P = vec3(P.x, -P.z, P.y);
    dPdx = vec3(dPdx.x, -dPdx.z, dPdx.y);
    dPdy = vec3(dPdy.x, -dPdy.z, dPdy.y);
  } else if (major_axis == P.x) {
    P = vec3(-P.z, -P.y, P.x);
    dPdx = vec3(-dPdx.z, -dPdx.y, dPdx.x);
    dPdy = vec3(-dPdy.z, -dPdy.y, dPdy.x);
  } else {
    P = vec3(P.z, -P.y, P.x);
    dPdx = vec3(dPdx.z, -dPdx.y, dPdx.x);
    dPdy = vec3(dPdy.z, -dPdy.y, dPdy.x);
  }

  vec2 duv_dx = 0.5f *
                (abs(P.z) * vec2(dPdx.x, dPdx.y) - vec2(P.x, P.y) * dPdx.z) /
                (P.z * P.z);
  vec2 duv_dy = 0.5f *
                (abs(P.z) * vec2(dPdy.x, dPdy.y) - vec2(P.x, P.y) * dPdy.z) /
                (P.z * P.z);

  vec2 mx = duv_dx * size;
  vec2 my = duv_dy * size;

  ellipse_transform_derivatives(mx, my);

  float len_x = length(mx);
  float len_y = length(my);

  float len_max = max(len_x, len_y);
  float len_min = min(len_x, len_y);

  float anisotropy = min(len_max / len_min, 16.0f);

  return log2(len_max / anisotropy);
}

#if GL_core_profile && !SLANG

inline vec3 sample_convolved_asg_isotropic(ASG asg,
                                           SampledTextureCube env_map) {
  const float MIN_SHARPNESS =
      roughness_to_asg_sharpness(MIN_CONVOLVED_SG_CUBE_MAP_ROUGHNESS);
  float sharpness = min(asg.lx, asg.ly);
  float ratio = sharpness / MIN_SHARPNESS;
  float reverse_mip = 0.5f * log2(ratio);
  float num_mips = texture_query_levels(env_map);
  float mip = num_mips - 1.0f - reverse_mip;
  return integrate_asg(asg) * texture_lod(env_map, asg.z, mip).rgb;
}

inline vec3
sample_convolved_asg_software_anisotropic(ASG asg, SampledTextureCube env_map) {
  vec2 sharpness = vec2(asg.lx, asg.ly);
  const float MIN_SHARPNESS =
      roughness_to_asg_sharpness(MIN_CONVOLVED_SG_CUBE_MAP_ROUGHNESS);
  vec2 ratio = sharpness / MIN_SHARPNESS;
  float max_ratio = max(ratio.x, ratio.y);
  float min_ratio = min(ratio.x, ratio.y);

  float rsqrt_min_ratio = 1.0f / sqrt(min_ratio);

  float cone_width = 2.0f * sqrt(2.0f / 3.0f) * rsqrt_min_ratio;
  vec3 axis_of_anisotropy = asg.lx < asg.ly ? asg.x : asg.y;

  const float MAX_ANISOTROPY = 16.0f;
  float anisotropy = min(sqrt(max_ratio) * rsqrt_min_ratio, MAX_ANISOTROPY);

  float ratio_bias = 1.0f;
#if FS
  vec3 dPdx = dFdx(asg.z);
  vec3 dPdy = dFdy(asg.z);
  float dPdx2 = dot(dPdx, dPdx);
  float dPdy2 = dot(dPdy, dPdy);
  ratio_bias = min(cone_width * cone_width / max(dPdx2, dPdy2), 1.0f);
#endif
  float max_anisotropic_ratio =
      min(min_ratio * MAX_ANISOTROPY * MAX_ANISOTROPY, max_ratio);
  float reverse_lod = 0.5f * log2(max_anisotropic_ratio * ratio_bias);
  float lod = texture_query_levels(env_map) - 1.0f - reverse_lod;

  vec3 conv = vec3(0.0f);
  uint num_samples = uint(ceil(anisotropy));
  for (uint i = 0; i < num_samples; ++i) {
    vec3 s = ((i + 1.0f) / (num_samples + 1.0f) - 0.5f) * cone_width *
             axis_of_anisotropy;
    conv += texture_lod(env_map, asg.z + s, lod).rgb;
  }
  conv /= num_samples;

  return integrate_asg(asg) * conv;
}

inline vec3 sample_convolved_asg(ASG asg, SampledTextureCube env_map) {
  return sample_convolved_asg_software_anisotropic(asg, env_map);
}

#endif

static const uint NUM_SG_ENV_LIGHTING_PARAMS = 6;
static const uint MAX_SG_ENV_LIGHTING_SIZE = 32;
static const uint MAX_NUM_SG_ENV_LIGHTING_PARAMS =
    NUM_SG_ENV_LIGHTING_PARAMS * MAX_SG_ENV_LIGHTING_SIZE;

GLSL_NAMESPACE_END
