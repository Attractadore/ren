import glsl;
#define SLANG 1
#define GL_core_profile 1
#include "bake-sg-brdf-lut.h"

[Differentiable]
double eval_asg(double phi, double a, double lx,
                    double ly, no_diff double f0, no_diff dvec3 V, no_diff dvec3 B, no_diff dvec3 L) {
  DASG asg = make_asg(phi, a, lx, ly, f0, V, B);
  return eval_asg(asg, L);
}

public __extern_cpp double ren_sg_brdf_loss(SgBrdfLossArgs args) {
  [MaxIters(NUM_PARAMS * MAX_NUM_SGS)]
  for (uint k = 0; k < NUM_PARAMS * args.g; ++k) {
    args.grad[k] = 0.0;
  }

  double loss = 0.0;
  for (uint i = 0; i < args.n; ++i) {
    double delta = 0.0;
    [MaxIters(MAX_NUM_SGS)]
    for (uint g = 0; g < args.g; ++g) {
      const double *p = args.params + g * NUM_PARAMS;
      delta += eval_asg(p[0], p[1], p[2], p[3], args.f0[i], args.V, args.B, args.L[i]);
    }
    delta = delta - args.y[i];
    loss += delta * delta;

    [MaxIters(MAX_NUM_SGS)]
    for (uint g = 0; g < args.g; ++g) {
      DifferentialPair<double> dp[NUM_PARAMS];
      for (uint k = 0; k < NUM_PARAMS; ++k) {
        dp[k] = diffPair(args.params[g * NUM_PARAMS + k]);
      }
      bwd_diff(eval_asg)(dp[0], dp[1], dp[2], dp[3], args.f0[i], args.V, args.B, args.L[i], 2.0 * delta);
      for (uint k = 0; k < NUM_PARAMS; ++k) {
        args.grad[g * NUM_PARAMS + k] += dp[k].d;
      }
    }
  }

  return loss;
}
