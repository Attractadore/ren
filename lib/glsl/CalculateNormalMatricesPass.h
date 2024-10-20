#ifndef REN_GLSL_CALCULATE_NORMAL_MATRICES_PASS_H
#define REN_GLSL_CALCULATE_NORMAL_MATRICES_PASS_H

#include "Common.h"
#include "DevicePtr.h"

GLSL_NAMESPACE_BEGIN

const uint CALCULATE_NORMAL_MATRICES_THREADS = 128;

struct CalculateNormalMatricesPassArgs {
  GLSL_PTR(mat4x3) transforms;
  GLSL_PTR(mat3) normals;
  int pad;
};

GLSL_NAMESPACE_END

#endif // REN_GLSL_CALCULATE_NORMAL_MATRICES_PASS_H
