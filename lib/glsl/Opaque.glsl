#ifndef REN_GLSL_OPAQUE_GLSL
#define REN_GLSL_OPAQUE_GLSL

#include "Opaque.h"

SPEC_CONSTANT(S_OPAQUE_FEATURE_VC) bool OPAQUE_FEATURE_VC = false;
SPEC_CONSTANT(S_OPAQUE_FEATURE_UV) bool OPAQUE_FEATURE_UV = true;
SPEC_CONSTANT(S_OPAQUE_FEATURE_TS) bool OPAQUE_FEATURE_TS = true;

PUSH_CONSTANTS(OpaqueArgs);

const uint V_POSITION = 0;
const uint V_NORMAL = 1;
const uint V_TANGENT = 2;
const uint V_UV = 3;
const uint V_COLOR = 4;
const uint V_MATERIAL = 5;

#endif // REN_GLSL_OPAQUE_GLSL
