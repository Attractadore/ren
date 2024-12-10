#ifndef REN_GLSL_OPAQUE_H
#define REN_GLSL_OPAQUE_H

#include "Common.h"
#include "DevicePtr.h"
#include "Lighting.h"
#include "Material.h"
#include "Mesh.h"

GLSL_NAMESPACE_BEGIN

struct OpaqueArgs {
  GLSL_PTR(Mesh) meshes;
  GLSL_PTR(MeshInstance) mesh_instances;
  GLSL_PTR(mat4x3) transform_matrices;
  GLSL_PTR(mat3) normal_matrices;
  GLSL_PTR(Material) materials;
  GLSL_PTR(DirectionalLight) directional_lights;
  uint num_directional_lights;
  mat4 proj_view;
  vec3 eye;
  Texture2D exposure;
};

const uint S_OPAQUE_FEATURE_VC = 0;
const uint S_OPAQUE_FEATURE_UV = 1;
const uint S_OPAQUE_FEATURE_TS = 2;

GLSL_NAMESPACE_END

#endif // REN_GLSL_OPAQUE_H
