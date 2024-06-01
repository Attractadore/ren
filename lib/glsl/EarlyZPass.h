#ifndef REN_GLSL_EARLY_Z_PASS_H
#define REN_GLSL_EARLY_Z_PASS_H

#include "BufferReference.h"
#include "Common.h"
#include "Mesh.h"

GLSL_NAMESPACE_BEGIN

struct EarlyZPassConstants {
  GLSL_REF(MeshRef) meshes;
  GLSL_REF(MeshInstanceRef) mesh_instances;
  GLSL_REF(TransformMatrixRef) transform_matrices;
  mat4 proj_view;
};

GLSL_NAMESPACE_END

#endif // REN_GLSL_EARLY_Z_PASS_H
