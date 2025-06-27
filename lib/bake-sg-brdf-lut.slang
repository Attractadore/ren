import glsl;
#define SLANG 1
#define GL_core_profile 1
#include "bake-sg-brdf-lut.h"

[Differentiable]
float eval_asg(float phi, float a, float lx,
                    float ly, no_diff float f0, no_diff vec3 V, no_diff vec3 B, no_diff vec3 L) {
  ASG asg = make_asg(phi, a, lx, ly, f0, V, B);
  return eval_asg(asg, L);
}

public __extern_cpp float ren_sg_brdf_loss(SgBrdfLossArgs args) {
  [MaxIters(NUM_PARAMS * MAX_NUM_SGS)]
  for (uint k = 0; k < NUM_PARAMS * args.g; ++k) {
    args.grad[k] = 0.0f;
  }

  float loss = 0.0f;
  for (uint i = 0; i < args.n; ++i) {
    float delta = 0.0f;
    [MaxIters(MAX_NUM_SGS)]
    for (uint g = 0; g < args.g; ++g) {
      const float *p = args.params + g * NUM_PARAMS;
      delta += eval_asg(p[0], p[1], p[2], p[3], args.f0[i], args.V, args.B, args.L[i]);
    }
    delta = delta - args.y[i];
    loss += delta * delta;

    [MaxIters(MAX_NUM_SGS)]
    for (uint g = 0; g < args.g; ++g) {
      DifferentialPair<float> dp[NUM_PARAMS];
      for (uint k = 0; k < NUM_PARAMS; ++k) {
        dp[k] = diffPair(args.params[g * NUM_PARAMS + k]);
      }
      bwd_diff(eval_asg)(dp[0], dp[1], dp[2], dp[3], args.f0[i], args.V, args.B, args.L[i], 2.0f * delta);
      for (uint k = 0; k < NUM_PARAMS; ++k) {
        args.grad[g * NUM_PARAMS + k] += dp[k].d;
      }
    }
  }

  return loss;
}
