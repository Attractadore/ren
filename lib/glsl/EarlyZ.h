#ifndef REN_GLSL_EARLY_Z_H
#define REN_GLSL_EARLY_Z_H

#include "Common.h"
#include "DevicePtr.h"
#include "Mesh.h"

GLSL_NAMESPACE_BEGIN

struct EarlyZArgs {
  GLSL_PTR(Mesh) meshes;
  GLSL_PTR(MeshInstance) mesh_instances;
  GLSL_PTR(mat4x3) transform_matrices;
  mat4 proj_view;
};

GLSL_NAMESPACE_END

#endif // REN_GLSL_EARLY_Z_H
