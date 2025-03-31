#pragma once
#include "Culling.h"
#include "Indirect.h"
#include "Mesh.h"
#include "Std.h"
#include "Texture.h"

GLSL_NAMESPACE_BEGIN

const uint MESHLET_CULLING_CONE_BIT = 1 << 0;
const uint MESHLET_CULLING_FRUSTUM_BIT = 1 << 1;
const uint MESHLET_CULLING_OCCLUSION_BIT = 1 << 2;

GLSL_PUSH_CONSTANTS MeshletCullingArgs {
  GLSL_READONLY GLSL_PTR(Mesh) meshes;
  GLSL_READONLY GLSL_PTR(mat4x3) transform_matrices;
  /// Pointer to current bucket's cull data.
  GLSL_READONLY GLSL_PTR(MeshletCullData) bucket_cull_data;
  /// Pointer to current bucket's size.
  GLSL_PTR(uint) bucket_size;
  GLSL_PTR(uint) batch_sizes;
  GLSL_PTR(DispatchIndirectCommand) batch_prepare_commands;
  GLSL_PTR(MeshletDrawCommand) commands;
  GLSL_WRITEONLY GLSL_PTR(glsl_BatchId) command_batch_ids;
  GLSL_PTR(uint) num_commands;
  GLSL_PTR(DispatchIndirectCommand) sort_command;
  mat4 proj_view;
  vec3 eye;
  uint feature_mask;
  /// Current bucket index.
  uint bucket;
  SampledTexture2D hi_z;
}
GLSL_PC;

GLSL_NAMESPACE_END
