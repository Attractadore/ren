#pragma once
#include "DevicePtr.h"
#include "Lighting.h"
#include "Material.h"
#include "Mesh.h"
#include "Std.h"

GLSL_NAMESPACE_BEGIN

GLSL_PUSH_CONSTANTS OpaqueArgs {
  GLSL_READONLY GLSL_PTR(float) exposure;
  GLSL_READONLY GLSL_PTR(Mesh) meshes;
  GLSL_READONLY GLSL_PTR(MeshInstance) mesh_instances;
  GLSL_READONLY GLSL_PTR(mat4x3) transform_matrices;
  GLSL_READONLY GLSL_PTR(Material) materials;
  GLSL_READONLY GLSL_PTR(DirectionalLight) directional_lights;
  uint num_directional_lights;
  mat4 proj_view;
  float znear;
  vec3 eye;
  vec2 inv_viewport;
  SampledTexture2D ssao;
  vec3 env_luminance;
  SampledTextureCube raw_env_map;
}
GLSL_PC;

const uint S_OPAQUE_FEATURE_VC = 0;
const uint S_OPAQUE_FEATURE_UV = 1;
const uint S_OPAQUE_FEATURE_TS = 2;

#if GL_core_profile

SPEC_CONSTANT(S_OPAQUE_FEATURE_VC) bool OPAQUE_FEATURE_VC = false;
SPEC_CONSTANT(S_OPAQUE_FEATURE_UV) bool OPAQUE_FEATURE_UV = true;
SPEC_CONSTANT(S_OPAQUE_FEATURE_TS) bool OPAQUE_FEATURE_TS = true;

const uint A_POSITION = 0;
const uint A_NORMAL = 1;
const uint A_TANGENT = 2;
const uint A_UV = 3;
const uint A_COLOR = 4;
const uint A_MATERIAL = 5;

#endif

GLSL_NAMESPACE_END
