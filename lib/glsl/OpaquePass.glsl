#ifndef REN_GLSL_OPAQUE_PASS_GLSL
#define REN_GLSL_OPAQUE_PASS_GLSL

#include "OpaquePass.h"

layout(constant_id = S_OPAQUE_FEATURE_VC) const bool OPAQUE_FEATURE_VC = false;
layout(constant_id = S_OPAQUE_FEATURE_UV) const bool OPAQUE_FEATURE_UV = true;
layout(constant_id = S_OPAQUE_FEATURE_TS) const bool OPAQUE_FEATURE_TS = true;

PUSH_CONSTANTS GLSL_OPAQUE_CONSTANTS pc;

const uint V_POSITION = 0;
const uint V_NORMAL = 1;
const uint V_TANGENT = 2;
const uint V_UV = 3;
const uint V_COLOR = 4;
const uint V_MATERIAL = 5;

#endif // REN_GLSL_OPAQUE_PASS_GLSL
