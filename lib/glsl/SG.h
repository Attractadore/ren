#pragma once
#include "Std.h"
#include "Transforms.h"
#if GL_core_profile
#include "Texture.glsl"
#endif

GLSL_NAMESPACE_BEGIN

struct SG {
  vec3 z;
  float a;
  float l;
};

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

#if GL_core_profile && !SLANG

inline vec3 sample_convolved_asg(ASG asg, SampledTextureCube env_map) {
  const float MIN_SHARPNESS =
      roughness_to_asg_sharpness(MIN_CONVOLVED_SG_CUBE_MAP_ROUGHNESS);

#if 0
  float sharpness = min(asg.lx, asg.ly);
  float ratio = sharpness / MIN_SHARPNESS;
  float reverse_mip = 0.5f * log2(ratio);
  float num_mips = texture_query_levels(env_map);
  float mip = num_mips - 1.0f - reverse_mip;
  return integrate_asg(asg) * texture_lod(env_map, asg.z, mip).rgb;
#else
  vec3 axis = abs(asg.z);
  float major_axis = max(max(axis.x, axis.y), axis.z);

  vec3 X = asg.x;
  vec3 Y = asg.y;
  if (major_axis == axis.z) {
    X.z = 0.0f;
    Y.z = 0.0f;
    ellipse_transform_derivatives(X.xy, Y.xy);
  } else if (major_axis == axis.y) {
    X.y = 0.0f;
    Y.y = 0.0f;
    ellipse_transform_derivatives(X.xz, Y.xz);
  } else {
    X.x = 0.0f;
    Y.x = 0.0f;
    ellipse_transform_derivatives(X.yz, Y.yz);
  }
  X = normalize(X);
  Y = normalize(Y);

  vec2 sharpness = vec2(asg.lx, asg.ly);
  vec2 ratio = sharpness / MIN_SHARPNESS;
  vec2 len = 2.0f * major_axis / sqrt(ratio);

  vec3 dPdx = len.x * X;
  vec3 dPdy = len.y * Y;

  vec3 conv = texture_grad(env_map, asg.z, dPdx, dPdy).rgb;

  return integrate_asg(asg) * conv;
#endif
}

#endif

GLSL_NAMESPACE_END
