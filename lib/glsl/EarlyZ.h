#ifndef REN_GLSL_EARLY_Z_H
#define REN_GLSL_EARLY_Z_H

#include "DevicePtr.h"
#include "Mesh.h"
#include "Std.h"

GLSL_NAMESPACE_BEGIN

GLSL_PUSH_CONSTANTS EarlyZArgs {
  GLSL_READONLY GLSL_PTR(Mesh) meshes;
  GLSL_READONLY GLSL_PTR(MeshInstance) mesh_instances;
  GLSL_READONLY GLSL_PTR(mat4x3) transform_matrices;
  mat4 proj_view;
}
GLSL_PC;

GLSL_NAMESPACE_END

#endif // REN_GLSL_EARLY_Z_H
