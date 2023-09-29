#ifndef REN_GLSL_EARLY_Z_PASS_H
#define REN_GLSL_EARLY_Z_PASS_H

#include "Mesh.h"
#include "common.h"

GLSL_NAMESPACE_BEGIN

#define GLSL_EARLY_Z_CONSTANTS                                                 \
  {                                                                            \
    GLSL_RESTRICT GLSL_READONLY GLSL_BUFFER_REFERENCE(Positions) positions;    \
    GLSL_RESTRICT GLSL_READONLY GLSL_BUFFER_REFERENCE(TransformMatrices)       \
        transform_matrices;                                                    \
    mat4 pv;                                                                   \
  }

GLSL_NAMESPACE_END

#endif // REN_GLSL_EARLY_Z_PASS_H
