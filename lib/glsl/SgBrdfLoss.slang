import glsl;
#define SLANG 1
#define GL_core_profile 1
#include "BRDF.h"
#include "Random.h"
#include "SG.h"
#include "SgBrdfLoss.h"

[vk::push_constant]
SgBrdfLossArgs pc;

groupshared float params[MAX_SG_BRDF_PARAMS];
groupshared float grad[MAX_SG_BRDF_PARAMS];

[Differentiable]
float F_norm(no_diff float f0, float NoV) {
  int i = (int)round(f0 * float(F_NORM_LUT_SIZE - 1));
  return pc.f_norm_lut[i] * F_schlick(f0, NoV);
};

[Differentiable]
ASG make_asg(float phi, float a, float lx, float ly, float f0, float roughness,
             vec3 V) {
  vec3 Z = { cos(phi), 0, sin(phi) };
  vec3 Y = { 0, 1, 0 };
  vec3 X = { -sin(phi), 0, cos(phi) };
  vec3 H = normalize(Z + V);
  float VoH = dot(V, H);
  float NoV = V.z;
  float NoH = H.z;
  ASG asg = {};
  asg.z = Z;
  asg.x = X;
  asg.y = Y;
  asg.a = a * F_norm(f0, VoH) * D_ggx(roughness, NoH) / (4 * NoV);
  float alpha2 = roughness * roughness;
  alpha2 = alpha2 * alpha2;
  float l = 2 / alpha2;
  asg.lx = (lx * lx) * (l / 8);
  asg.ly = (ly * ly) * (l / (8 * NoV * NoV));
  return asg;
};

[Differentiable]
float eval_asg(float phi, float a, float lx, float ly, no_diff float f0,
               no_diff float roughness, no_diff vec3 V, no_diff vec3 L) {
  ASG asg = make_asg(phi, a, lx, ly, f0, roughness, V);
  return eval_asg(asg, L);
}

[shader("compute")]
[numthreads(32, 1, 1)]
void main() {
  vec3 V = { sqrt(1 - pc.NoV * pc.NoV), 0, pc.NoV };
  vec3 Xi = r3_seq(gl_GlobalInvocationID.x);
  float f0 = mix(MIN_F0, 1.0f, Xi.x);

  float loss;

  if (gl_LocalInvocationIndex < pc.g * NUM_SG_BRDF_PARAMS) {
    params[gl_LocalInvocationIndex] = pc.params[gl_LocalInvocationIndex];
    grad[gl_LocalInvocationIndex] = 0.0f;
  }
  barrier();

  {
    vec3 H = importance_sample_ggx(Xi.yz, pc.roughness);
    float NoH = H.z;
    float VoH = dot(V, H);
    vec3 L = 2.0f * VoH * H - V;
    float NoL = L.z;

    float F = F_norm(f0, VoH);
    float G = G_smith(pc.roughness, NoL, pc.NoV);
    float D = D_ggx(pc.roughness, NoH);
    float Q = 4.0f * pc.NoV;

    float y = NoL > 0.0f ? F * G * D / Q : 0.0f;
    float w = (4.0f * max(VoH, 0)) / (D * NoH);

    float p = 0.0f;
    [MaxIters(MAX_SG_BRDF_SIZE)]
    for (uint g = 0; g < pc.g; ++g) {
      uint k = g * NUM_SG_BRDF_PARAMS;
      p += eval_asg(params[k + 0], params[k + 1], params[k + 2], params[k + 3],
                    f0, pc.roughness, V, L);
    }
    float delta = p - y;
    loss = WaveActiveSum(delta * delta * w);

    [MaxIters(MAX_SG_BRDF_SIZE)]
    for (uint g = 0; g < pc.g; ++g) {
      DifferentialPair<float> dp[NUM_SG_BRDF_PARAMS];
      for (uint k = 0; k < NUM_SG_BRDF_PARAMS; ++k) {
        dp[k] = diffPair(params[g * NUM_SG_BRDF_PARAMS + k]);
      }
      bwd_diff(eval_asg)(dp[0], dp[1], dp[2], dp[3], f0, pc.roughness, V, L,
                         2.0f * delta * w);
      for (uint k = 0; k < NUM_SG_BRDF_PARAMS; ++k) {
        float s = WaveActiveSum(dp[k].d);
        if (WaveIsFirstLane()) {
          grad[g * NUM_SG_BRDF_PARAMS + k] = s;
        }
      }
    }
  }
  {
    vec3 L = uniform_sample_sphere(Xi.yz);
    vec3 H = normalize(L + V);
    float NoH = H.z;
    float VoH = dot(V, H);
    float NoL = L.z;

    float F = F_norm(f0, VoH);
    float G = G_smith(pc.roughness, NoL, pc.NoV);
    float D = D_ggx(pc.roughness, NoH);
    float Q = 4.0f * pc.NoV;

    float y = NoL > 0.0f ? F * G * D / Q : 0.0f;
    float w = 4.0f * PI;

    float p = 0.0f;
    [MaxIters(MAX_SG_BRDF_SIZE)]
    for (uint g = 0; g < pc.g; ++g) {
      uint k = g * NUM_SG_BRDF_PARAMS;
      p += eval_asg(params[k + 0], params[k + 1], params[k + 2], params[k + 3],
                    f0, pc.roughness, V, L);
    }
    float delta = p - y;
    loss += WaveActiveSum(delta * delta * w);

    [MaxIters(MAX_SG_BRDF_SIZE)]
    for (uint g = 0; g < pc.g; ++g) {
      DifferentialPair<float> dp[NUM_SG_BRDF_PARAMS];
      for (uint k = 0; k < NUM_SG_BRDF_PARAMS; ++k) {
        dp[k] = diffPair(params[g * NUM_SG_BRDF_PARAMS + k]);
      }
      bwd_diff(eval_asg)(dp[0], dp[1], dp[2], dp[3], f0, pc.roughness, V, L,
                         2.0f * delta * w);
      for (uint k = 0; k < NUM_SG_BRDF_PARAMS; ++k) {
        float s = WaveActiveSum(dp[k].d);
        if (WaveIsFirstLane()) {
          grad[g * NUM_SG_BRDF_PARAMS + k] += s;
        }
      }
    }
  }
  barrier();

  if (WaveIsFirstLane()) {
    pc.loss[gl_WorkGroupID.x].value = loss;
  }

  if (gl_LocalInvocationIndex < pc.g * NUM_SG_BRDF_PARAMS) {
    pc.grad[pc.n / 32 * gl_LocalInvocationIndex + gl_WorkGroupID.x].value =
        grad[gl_LocalInvocationIndex];
  }
}
