#pragma once
#include "Std.h"

GLSL_NAMESPACE_BEGIN

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

GLSL_NAMESPACE_END
