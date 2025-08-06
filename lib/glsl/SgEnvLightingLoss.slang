import glsl;
#define SLANG 1
#define GL_core_profile 1
#include "BRDF.h"
#include "Random.h"
#include "SG.h"
#include "SgEnvLightingLoss.h"
#include "Transforms.h"

[[vk::push_constant]]
SgEnvLightingLossArgs pc;
groupshared float gs_params[MAX_NUM_SG_ENV_LIGHTING_PARAMS];
groupshared float gs_grad[MAX_NUM_SG_ENV_LIGHTING_PARAMS];
groupshared float gs_loss;

[Differentiable]
vec3 eval_sg(float phi, float z, float amp_r, float amp_g, float amp_b,
             float sh, no_diff vec3 V) {
  SG3 sg;
  sg.z = cylindrical_to_cartesian(phi, z);
  sg.a = vec3(amp_r, amp_g, amp_b);
  sg.l = sh;
  return eval_sg(sg, V);
}

[shader("compute")]
[numthreads(SG_ENV_LIGHTING_LOSS_THREADS, 1, 1)]
void main() {
  uint num_params = pc.num_sgs * NUM_SG_ENV_LIGHTING_PARAMS;
  if (gl_LocalInvocationIndex < num_params) {
    gs_params[gl_LocalInvocationIndex] = pc.params[gl_LocalInvocationIndex];
    gs_grad[gl_LocalInvocationIndex] = 0.0f;
  }
  if (gl_LocalInvocationIndex == 0) {
    gs_loss = 0.0f;
  }
  barrier();

  vec3 R = uniform_sample_sphere(r2_seq(gl_GlobalInvocationID.x));
  vec3 P = 0.0f;
  [MaxIters(MAX_SG_ENV_LIGHTING_SIZE)]
  for (uint i = 0; i < pc.num_sgs; ++i) {
    uint p = i * NUM_SG_ENV_LIGHTING_PARAMS;
    float phi = gs_params[p + 0];
    float z = gs_params[p + 1];
    float amp_r = gs_params[p + 2];
    float amp_g = gs_params[p + 3];
    float amp_b = gs_params[p + 4];
    float sh = gs_params[p + 5];
    P += eval_sg(phi, z, amp_r, amp_g, amp_b, sh, R);
  }
  vec3 L;
  L.x = pc.luminance[gl_GlobalInvocationID.x * 3 + 0];
  L.y = pc.luminance[gl_GlobalInvocationID.x * 3 + 1];
  L.z = pc.luminance[gl_GlobalInvocationID.x * 3 + 2];
  vec3 D = P - L;

  float loss = dot(D, D);
  if (isnan(loss)) {
    printf("%d: R: (%f, %f, %f), L: (%f, %f, %f)\n", gl_GlobalInvocationID.x,
           R.x, R.y, R.z, L.x, L.y, L.z);
  }
  loss = subgroupAdd(loss);
  if (subgroupElect()) {
    atomicAdd(gs_loss, loss);
  }

  [MaxIters(MAX_SG_ENV_LIGHTING_SIZE)]
  for (uint i = 0; i < pc.num_sgs; ++i) {
    DifferentialPair<float> dp[NUM_SG_ENV_LIGHTING_PARAMS];
    for (uint k = 0; k < NUM_SG_ENV_LIGHTING_PARAMS; ++k) {
      dp[k] = diffPair(gs_params[i * NUM_SG_ENV_LIGHTING_PARAMS + k]);
    }
    bwd_diff(eval_sg)(dp[0], dp[1], dp[2], dp[3], dp[4], dp[5], R, 2.0f * D);
    for (uint k = 0; k < NUM_SG_ENV_LIGHTING_PARAMS; ++k) {
      float grad = subgroupAdd(dp[k].d);
      if (subgroupElect()) {
        atomicAdd(gs_grad[i * NUM_SG_ENV_LIGHTING_PARAMS + k], grad);
      }
    }
  }
  barrier();

  uint num_batches = gl_NumWorkGroups.x;
  uint batch_index = gl_WorkGroupID.x;
  if (gl_LocalInvocationIndex < num_params) {
    pc.grad[num_batches * gl_LocalInvocationIndex + batch_index].value =
        gs_grad[gl_LocalInvocationIndex];
  }
  if (gl_LocalInvocationIndex == 0) {
    pc.loss[batch_index].value = gs_loss;
  }
}
