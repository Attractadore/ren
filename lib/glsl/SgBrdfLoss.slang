import glsl;
#define SLANG 1
#define GL_core_profile 1

[Differentiable]
double cos(double phi) { return cos(float(phi)); }

[Differentiable]
double sin(double phi) {
  return sin(float(phi));
}

#include "BRDF.h"
#include "Random.h"
#include "SgBrdfLoss.h"

[vk::push_constant] SgBrdfLossArgs pc;

[Differentiable] double F_norm(no_diff double f0, double NoV) {
  int i = round(f0 * double(F_NORM_LUT_SIZE - 1));
  return pc.f_norm_lut[i] * F_schlick(f0, NoV);
};

struct DASG : IDifferentiable {
  dvec3 z;
  dvec3 x;
  dvec3 y;
  double a;
  double lx;
  double ly;
};

[Differentiable]
double eval_asg(DASG asg, dvec3 V) {
  double VoX = dot(asg.x, V);
  double VoY = dot(asg.y, V);
  return asg.a * max(dot(asg.z, V), 0.0) *
         exp(float(-asg.lx * VoX * VoX - asg.ly * VoY * VoY));
}

[Differentiable]
DASG make_asg(double phi, double a, double lx, double ly, double f0, double roughness, dvec3 V) {
  dvec3 Z = {cos(phi), 0, sin(phi)};
  dvec3 Y = dvec3(0, 1, 0);
  dvec3 X = {-sin(phi), 0, cos(phi)};
  dvec3 H = normalize(Z + V);
  double VoH = dot(V, H);
  double NoV = V.z;
  double NoL = Z.z;
  double NoH = H.z;
  DASG asg = {};
  asg.z = Z;
  asg.x = X;
  asg.y = Y;
  asg.a = a * F_norm(f0, VoH) * D_ggx(roughness, NoH);
  double alpha2 = roughness * roughness;
  alpha2 = alpha2 * alpha2;
  double l = 2 / alpha2;
  asg.lx = (lx * lx) * (l / 8);
  asg.ly = (ly * ly) * (l / (8 * NoV * NoV));
  return asg;
};

[Differentiable]
double eval_asg(double phi, double a, double lx, double ly, no_diff double f0, no_diff double roughness, no_diff dvec3 V, no_diff dvec3 L) {
  DASG asg = make_asg(phi, a, lx, ly, f0, roughness, V);
  return eval_asg(asg, L);
}

[shader("compute")]
[numthreads(32, 1, 1)]
void main() {
  dvec3 V = dvec3(sqrt(1 - pc.NoV * pc.NoV), 0, pc.NoV);

  float i = gl_GlobalInvocationID.x;

  dvec3 Xi = r3_seq(i);
  double f0 = mix(MIN_F0, 1, Xi.x);
  dvec3 H = importance_sample_ggx(Xi.yz, pc.roughness);
  double NoH = H.z;
  double VoH = dot(V, H);
  dvec3 L = 2 * VoH * H - V;
  double NoL = L.z;

  double F = F_norm(f0, VoH);
  double G = G_smith(pc.roughness, NoL, pc.NoV);
  double D = D_ggx(pc.roughness, NoH);
  double Q = 4 * pc.NoV;

  double y = NoL > 0 ? F * G * D / Q : 0;
  double w = (4 * max(VoH, 0)) / (D * NoH);

  double delta = 0.0;
  [MaxIters(MAX_NUM_SGS)]
  for (uint g = 0; g < pc.g; ++g) {
    const double *p = pc.params + g * NUM_PARAMS;
    delta += eval_asg(p[0], p[1], p[2], p[3], f0, pc.roughness, V, L);
  }
  delta = delta - y;

  [MaxIters(MAX_NUM_SGS)]
  for (uint g = 0; g < pc.g; ++g) {
    DifferentialPair<double> dp[NUM_PARAMS];
    for (uint k = 0; k < NUM_PARAMS; ++k) {
      dp[k] = diffPair(pc.params[g * NUM_PARAMS + k]);
    }
    bwd_diff(eval_asg)(dp[0], dp[1], dp[2], dp[3], f0, pc.roughness, V, L,
                       2 * delta * w);
    for (uint k = 0; k < NUM_PARAMS; ++k) {
      double s = subgroupAdd(dp[k].d);
      if (subgroupElect()) {
        pc.grad[(gl_WorkGroupID.x * pc.g + g) * NUM_PARAMS + k].value = s;
      }
    }
  }

  double loss = subgroupAdd(delta * delta * w);
  double loss0 = subgroupAdd(y * y * w);
  if (subgroupElect()) {
    pc.loss[gl_WorkGroupID.x].value = loss;
    pc.loss0[gl_WorkGroupID.x].value = loss0;
  }
}
