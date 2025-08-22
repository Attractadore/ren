#pragma once
#include "Geometry.h"
#include "Lighting.h"
#include "Std.h"

namespace ren::sh {

struct OpaqueArgs {
  DevicePtr<float> exposure;
  DevicePtr<Mesh> meshes;
  DevicePtr<MeshInstance> mesh_instances;
  DevicePtr<mat4x3> transform_matrices;
  DevicePtr<Material> materials;
  DevicePtr<DirectionalLight> directional_lights;
  uint num_directional_lights;
  mat4 proj_view;
  float znear;
  vec3 eye;
  vec2 inv_viewport;
  Handle<Sampler2D> ssao;
  vec3 env_luminance;
  SH_RG_IGNORE(Handle<SamplerCube>) env_map;
};

static const uint S_OPAQUE_FEATURE_VC = 0;
static const uint S_OPAQUE_FEATURE_UV = 1;
static const uint S_OPAQUE_FEATURE_TS = 2;

#if __SLANG__

[vk::constant_id(S_OPAQUE_FEATURE_VC)] const bool OPAQUE_FEATURE_VC = false;
[vk::constant_id(S_OPAQUE_FEATURE_UV)] const bool OPAQUE_FEATURE_UV = true;
[vk::constant_id(S_OPAQUE_FEATURE_TS)] const bool OPAQUE_FEATURE_TS = true;

struct OpaqueVsOutput {
  vec4 sv_position : SV_Position;
  vec3 position : Position0;
  vec3 normal : Normal;
  vec4 tangent : Trangent;
  vec2 uv : TexCoord0;
  vec4 color : Color0;
  nointerpolation uint material : Material0;
};

#endif

} // namespace ren::sh
