#ifndef REN_GLSL_OPAQUE_PASS_H
#define REN_GLSL_OPAQUE_PASS_H

#include "Common.h"
#include "Lighting.h"
#include "Material.h"
#include "Mesh.h"

GLSL_NAMESPACE_BEGIN

GLSL_REF_TYPE(8) OpaqueUniformBufferRef {
  GLSL_REF(MeshRef) meshes;
  GLSL_REF(MeshInstanceRef) mesh_instances;
  GLSL_REF(TransformMatrixRef) transform_matrices;
  GLSL_REF(NormalMatrixRef) normal_matrices;
  mat4 proj_view;
};

struct OpaquePassArgs {
  GLSL_REF(OpaqueUniformBufferRef) ub;
  GLSL_REF(MaterialRef) materials;
  GLSL_REF(DirectionalLightRef) directional_lights;
  uint num_directional_lights;
  vec3 eye;
  uint exposure_texture;
};

const uint S_OPAQUE_FEATURE_VC = 0;
const uint S_OPAQUE_FEATURE_UV = 1;
const uint S_OPAQUE_FEATURE_TS = 2;

GLSL_NAMESPACE_END

#endif // REN_GLSL_OPAQUE_PASS_H
