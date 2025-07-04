import glsl;
#define SLANG 1
#define GL_core_profile 1
#include "bake-sg-brdf-lut.h"
#include "glsl/Random.h"

[Differentiable]
double eval_asg(double phi, double a, double lx,
                    double ly, no_diff double f0, no_diff double roughness, no_diff dvec3 V, no_diff dvec3 L) {
  DASG asg = make_asg(phi, a, lx, ly, f0, roughness, V);
  return eval_asg(asg, L);
}

public __extern_cpp double ren_sg_brdf_loss(SgBrdfLossArgs args) {
  [MaxIters(NUM_PARAMS * MAX_NUM_SGS)]
  for (uint k = 0; k < NUM_PARAMS * args.g; ++k) {
    args.grad[k] = 0.0;
  }

  dvec3 V = dvec3(sqrt(1 - args.NoV * args.NoV), 0, args.NoV);

  double loss = 0;
  double loss0 = 0;
  for (uint i = 0; i < args.n; ++i) {
    dvec3 Xi = r3_seq(i);
    double f0 = mix(MIN_F0, 1, Xi.x);
    dvec3 H = importance_sample_ggx(Xi.yz, args.roughness);
    double NoH = H.z;
    double VoH = dot(V, H);
    dvec3 L = 2 * VoH * H - V;
    double NoL = L.z;

    double F = F_norm(f0, VoH);
    double G = G_smith(args.roughness, NoL, args.NoV);
    double D = D_ggx(args.roughness, NoH);
    double Q = 4 * args.NoV;

    double y = NoL > 0 ? F * G * D / Q : 0;
    double w = (4 * max(VoH, 0)) / (D * NoH);

    double delta = 0.0;
    [MaxIters(MAX_NUM_SGS)]
    for (uint g = 0; g < args.g; ++g) {
      const double *p = args.params + g * NUM_PARAMS;
      delta += eval_asg(p[0], p[1], p[2], p[3], f0, args.roughness, V, L);
    }
    delta = delta - y;
    loss += delta * delta * w;
    loss0 += y * y * w;

    [MaxIters(MAX_NUM_SGS)]
    for (uint g = 0; g < args.g; ++g) {
      DifferentialPair<double> dp[NUM_PARAMS];
      for (uint k = 0; k < NUM_PARAMS; ++k) {
        dp[k] = diffPair(args.params[g * NUM_PARAMS + k]);
      }
      bwd_diff(eval_asg)(dp[0], dp[1], dp[2], dp[3], f0, args.roughness, V, L, 2 * delta * w);
      for (uint k = 0; k < NUM_PARAMS; ++k) {
        args.grad[g * NUM_PARAMS + k] += dp[k].d;
      }
    }
  }

  loss /= loss0;
  [MaxIters(NUM_PARAMS * MAX_NUM_SGS)]
  for (uint k = 0; k < NUM_PARAMS * args.g; ++k) {
    args.grad[k] /= loss0;
  }

  return loss;
}
