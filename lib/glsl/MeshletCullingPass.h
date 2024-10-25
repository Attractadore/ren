#ifndef REN_GLSL_MESHLET_CULLING_PASS_H
#define REN_GLSL_MESHLET_CULLING_PASS_H

#include "Common.h"
#include "Culling.h"
#include "Indirect.h"
#include "Mesh.h"
#include "Texture.h"

GLSL_NAMESPACE_BEGIN

const uint MESHLET_CULLING_CONE_BIT = 1 << 0;
const uint MESHLET_CULLING_FRUSTUM_BIT = 1 << 1;
const uint MESHLET_CULLING_OCCLUSION_BIT = 1 << 2;

struct MeshletCullingPassArgs {
  GLSL_PTR(Mesh) meshes;
  GLSL_PTR(mat4x3) transform_matrices;
  /// Pointer to current bucket's cull data.
  GLSL_PTR(MeshletCullData) bucket_cull_data;
  /// Pointer to current bucket's size.
  GLSL_PTR(uint) bucket_size;
  GLSL_PTR(DrawIndexedIndirectCommand) commands;
  GLSL_PTR(uint) command_offsets;
  GLSL_PTR(uint) command_counts;
  GLSL_PTR(mat4) proj_view;
  uint feature_mask;
  /// Current bucket index.
  uint bucket;
  vec3 eye;
  SampledTexture2D hi_z;
};

GLSL_NAMESPACE_END

#endif // REN_GLSL_MESHLET_CULLING_PASS_H
