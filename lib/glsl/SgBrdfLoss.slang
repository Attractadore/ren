import glsl;
#define SLANG 1
#define GL_core_profile 1
#include "BRDF.h"
#include "Random.h"
#include "SgBrdfLoss.h"

[vk::push_constant] SgBrdfLossArgs pc;

[Differentiable]
float F_norm(no_diff float f0, float NoV) {
  int i = round(f0 * float(F_NORM_LUT_SIZE - 1));
  return pc.f_norm_lut[i] * F_schlick(f0, NoV);
};

struct ASG : IDifferentiable {
  vec3 z;
  vec3 x;
  vec3 y;
  float a;
  float lx;
  float ly;
};

[Differentiable]
float eval_asg(ASG asg, vec3 V) {
  float VoX = dot(asg.x, V);
  float VoY = dot(asg.y, V);
  return asg.a * max(dot(asg.z, V), 0.0) * exp(-asg.lx * VoX * VoX - asg.ly * VoY * VoY);
}

[Differentiable]
ASG make_asg(float phi, float a, float lx, float ly, float f0, float roughness, vec3 V) {
  vec3 Z = {cos(phi), 0, sin(phi)};
  vec3 Y = vec3(0, 1, 0);
  vec3 X = {-sin(phi), 0, cos(phi)};
  vec3 H = normalize(Z + V);
  float VoH = dot(V, H);
  float NoV = V.z;
  float NoL = Z.z;
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
float eval_asg(float phi, float a, float lx, float ly, no_diff float f0, no_diff float roughness, no_diff vec3 V, no_diff vec3 L) {
  ASG asg = make_asg(phi, a, lx, ly, f0, roughness, V);
  return eval_asg(asg, L);
}

[shader("compute")]
[numthreads(32, 1, 1)]
void main() {
  vec3 V = vec3(sqrt(1 - pc.NoV * pc.NoV), 0, pc.NoV);

  float i = gl_GlobalInvocationID.x;

  vec3 Xi = r3_seq(i);
  float f0 = mix(MIN_F0, 1, Xi.x);
  vec3 H = importance_sample_ggx(Xi.yz, pc.roughness);
  float NoH = H.z;
  float VoH = dot(V, H);
  vec3 L = 2 * VoH * H - V;
  float NoL = L.z;

  float F = F_norm(f0, VoH);
  float G = G_smith(pc.roughness, NoL, pc.NoV);
  float D = D_ggx(pc.roughness, NoH);
  float Q = 4 * pc.NoV;

  float y = NoL > 0 ? F * G * D / Q : 0;
  float w = (4 * max(VoH, 0)) / (D * NoH);

  float delta = 0.0;
  [MaxIters(MAX_NUM_SGS)]
  for (uint g = 0; g < pc.g; ++g) {
    const float *p = pc.params + g * NUM_PARAMS;
    delta += eval_asg(p[0], p[1], p[2], p[3], f0, pc.roughness, V, L);
  }
  delta = delta - y;

  [MaxIters(MAX_NUM_SGS)]
  for (uint g = 0; g < pc.g; ++g) {
    DifferentialPair<float> dp[NUM_PARAMS];
    for (uint k = 0; k < NUM_PARAMS; ++k) {
      dp[k] = diffPair(pc.params[g * NUM_PARAMS + k]);
    }
    bwd_diff(eval_asg)(dp[0], dp[1], dp[2], dp[3], f0, pc.roughness, V, L,
                       2 * delta * w);
    for (uint k = 0; k < NUM_PARAMS; ++k) {
      float s = subgroupAdd(dp[k].d);
      if (subgroupElect()) {
        pc.grad[pc.n / 32 * (g * NUM_PARAMS + k) + gl_WorkGroupID.x].value = s;
      }
    }
  }

  float loss = subgroupAdd(delta * delta * w);
  float loss0 = subgroupAdd(y * y * w);
  if (subgroupElect()) {
    pc.loss[gl_WorkGroupID.x].value = loss;
    pc.loss0[gl_WorkGroupID.x].value = loss0;
  }
}
