import glsl;
#define SLANG 1
#define GL_core_profile 1
#include "BRDF.h"
#include "Random.h"
#include "SG.h"
#include "SgBrdfLoss.h"

[vk::push_constant]
SgBrdfLossArgs pc;

groupshared float gs_params[MAX_SG_BRDF_PARAMS];
groupshared float gs_grad[MAX_SG_BRDF_PARAMS];
groupshared float gs_loss;

[Differentiable]
float F_norm(no_diff float f0, float NoV) {
  return pc.f_norm_lut[gl_GlobalInvocationID.z] * F_schlick(f0, NoV);
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
[numthreads(SG_BRDF_LOSS_THREADS_X)]
void main() {
  if (gl_LocalInvocationIndex < pc.g * NUM_SG_BRDF_PARAMS) {
    gs_params[gl_LocalInvocationIndex] = pc.params[gl_LocalInvocationIndex];
    gs_grad[gl_LocalInvocationIndex] = 0.0f;
  }
  if (gl_LocalInvocationIndex == 0) {
    gs_loss = 0.0f;
  }
  barrier();

  float f0 = SG_BRDF_LOSS_F0[gl_GlobalInvocationID.z];

  vec3 V = { sqrt(1 - pc.NoV * pc.NoV), 0, pc.NoV };
  vec2 Xi = r2_seq(gl_GlobalInvocationID.x);
  vec3 L;
  vec3 H;
  switch ((SgBrdfLossPdf)gl_GlobalInvocationID.y) {
  case SgBrdfLossPdf::GGX: {
    H = importance_sample_ggx(Xi, pc.roughness);
    L = 2.0f * dot(V, H) * H - V;
    break;
  }
  case SgBrdfLossPdf::UniformSphere: {
    L = uniform_sample_sphere(Xi);
    H = normalize(L + V);
    break;
  }
  case SgBrdfLossPdf::Fresnel: {
    H = importance_sample_schlick(Xi, f0, V);
    L = 2.0f * dot(V, H) * H - V;
    break;
  }
  }

  float NoH = H.z;
  float VoH = dot(V, H);
  float NoL = L.z;

  float F = F_norm(f0, VoH);
  float G = G_smith(pc.roughness, NoL, pc.NoV);
  float D = D_ggx(pc.roughness, NoH);
  float Q = 4.0f * pc.NoV;

  float y = NoL > 0.0f ? F * G * D / Q : 0.0f;

  float w_ggx = NoH > 0.0f && VoH > 0.0f ? (4.0f * VoH) / (D * NoH) : 0.0f;
  float w_us = 4.0f * PI;
  float w_schlick = VoH > 0.0f ? (4.0f * VoH) / F_schlick_pdf(f0, VoH) : 0.0f;
  float w = SG_BRDF_LOSS_F0_WEIGHTS[gl_LocalInvocationID.z] *
            (w_ggx + w_us + w_schlick) / gl_WorkGroupSize.y;

  float p = 0.0f;
  [MaxIters(MAX_SG_BRDF_SIZE)]
  for (uint g = 0; g < pc.g; ++g) {
    uint k = g * NUM_SG_BRDF_PARAMS;
    p += eval_asg(gs_params[k + 0], gs_params[k + 1], gs_params[k + 2],
                  gs_params[k + 3], f0, pc.roughness, V, L);
  }

  float delta = p - y;
  float dloss_dp = 2.0f * delta * w;

  float loss = subgroupAdd(delta * delta * w);
  if (subgroupElect()) {
    atomicAdd(gs_loss, loss);
  }

  [MaxIters(MAX_SG_BRDF_SIZE)]
  for (uint g = 0; g < pc.g; ++g) {
    DifferentialPair<float> dp[NUM_SG_BRDF_PARAMS];
    for (uint k = 0; k < NUM_SG_BRDF_PARAMS; ++k) {
      dp[k] = diffPair(gs_params[g * NUM_SG_BRDF_PARAMS + k]);
    }
    bwd_diff(eval_asg)(dp[0], dp[1], dp[2], dp[3], f0, pc.roughness, V, L,
                       dloss_dp);
    for (uint k = 0; k < NUM_SG_BRDF_PARAMS; ++k) {
      float grad = subgroupAdd(dp[k].d);
      if (subgroupElect()) {
        atomicAdd(gs_grad[g * NUM_SG_BRDF_PARAMS + k], grad);
      }
    }
  }
  barrier();

  uint num_batches =
      gl_NumWorkGroups.x * gl_NumWorkGroups.y * gl_NumWorkGroups.z;
  uint batch_index =
      gl_WorkGroupID.z * gl_NumWorkGroups.x * gl_NumWorkGroups.y +
      gl_WorkGroupID.y * gl_NumWorkGroups.x + gl_WorkGroupID.x;

  if (gl_LocalInvocationIndex == 0) {
    pc.loss[batch_index].value = gs_loss;
  }

  if (gl_LocalInvocationIndex < pc.g * NUM_SG_BRDF_PARAMS) {
    pc.grad[num_batches * gl_LocalInvocationIndex + batch_index].value =
        gs_grad[gl_LocalInvocationIndex];
  }
}
