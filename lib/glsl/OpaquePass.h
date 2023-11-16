#ifndef REN_GLSL_OPAQUE_PASS_H
#define REN_GLSL_OPAQUE_PASS_H

#include "Lighting.h"
#include "Material.h"
#include "Mesh.h"
#include "common.h"

GLSL_NAMESPACE_BEGIN

GLSL_BUFFER(8) OpaqueUniformBuffer {
  GLSL_RESTRICT GLSL_READONLY GLSL_BUFFER_REFERENCE(Materials) materials;
  GLSL_RESTRICT GLSL_READONLY GLSL_BUFFER_REFERENCE(DrawMeshInstances)
      mesh_instances;
  GLSL_RESTRICT GLSL_READONLY GLSL_BUFFER_REFERENCE(TransformMatrices)
      transform_matrices;
  GLSL_RESTRICT GLSL_READONLY GLSL_BUFFER_REFERENCE(NormalMatrices)
      normal_matrices;
  GLSL_RESTRICT GLSL_READONLY GLSL_BUFFER_REFERENCE(DirectionalLights)
      directional_lights;
  uint num_directional_lights;
  mat4 pv;
  vec3 eye;
  uint exposure_texture;
};

#define GLSL_OPAQUE_CONSTANTS                                                  \
  {                                                                            \
    GLSL_RESTRICT GLSL_READONLY GLSL_BUFFER_REFERENCE(Positions) positions;    \
    GLSL_RESTRICT GLSL_READONLY GLSL_BUFFER_REFERENCE(Normals) normals;        \
    GLSL_RESTRICT GLSL_READONLY GLSL_BUFFER_REFERENCE(Tangents) tangents;      \
    GLSL_RESTRICT GLSL_READONLY GLSL_BUFFER_REFERENCE(UVs) uvs;                \
    GLSL_RESTRICT GLSL_READONLY GLSL_BUFFER_REFERENCE(Colors) colors;          \
    GLSL_RESTRICT GLSL_READONLY GLSL_BUFFER_REFERENCE(OpaqueUniformBuffer) ub; \
  }

const uint S_OPAQUE_FEATURE_VC = 0;
const uint S_OPAQUE_FEATURE_UV = 1;
const uint S_OPAQUE_FEATURE_TS = 2;

GLSL_NAMESPACE_END

#endif // REN_GLSL_OPAQUE_PASS_H
