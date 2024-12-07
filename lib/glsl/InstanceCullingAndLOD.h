#ifndef REN_GLSL_INSTANCE_CULLING_AND_LOD_H
#define REN_GLSL_INSTANCE_CULLING_AND_LOD_H

#include "Array.h"
#include "Common.h"
#include "Culling.h"
#include "DevicePtr.h"
#include "GpuScene.h"
#include "Indirect.h"
#include "Mesh.h"
#include "Texture.h"

GLSL_NAMESPACE_BEGIN

const uint INSTANCE_CULLING_AND_LOD_THREADS = 128;

const uint INSTANCE_CULLING_AND_LOD_LOD_SELECTION_BIT = 1 << 0;
const uint INSTANCE_CULLING_AND_LOD_FRUSTUM_BIT = 1 << 1;

const uint INSTANCE_CULLING_AND_LOD_FIRST_PHASE_BIT = 1 << 2;
const uint INSTANCE_CULLING_AND_LOD_SECOND_PHASE_BIT = 1 << 3;
const uint INSTANCE_CULLING_AND_LOD_OCCLUSION_MASK =
    INSTANCE_CULLING_AND_LOD_FIRST_PHASE_BIT |
    INSTANCE_CULLING_AND_LOD_SECOND_PHASE_BIT;

const uint INSTANCE_CULLING_AND_LOD_NO_OCCLUSION_CULLING = 0;
// Phase 1: Reject not visible in previous frame. Perform culling (without
// occlusion check) and LOD selection. Draw.
const uint INSTANCE_CULLING_AND_LOD_FIRST_PHASE =
    INSTANCE_CULLING_AND_LOD_FIRST_PHASE_BIT;
// Phase 2: Perform culling (with occlusion check). Generate new visibility
// buffer. Select LOD and draw if not visible in previous frame.
const uint INSTANCE_CULLING_AND_LOD_SECOND_PHASE =
    INSTANCE_CULLING_AND_LOD_SECOND_PHASE_BIT;
// Phase 3+: Reject not visible in current frame. Select LOD. Draw.
const uint INSTANCE_CULLING_AND_LOD_THIRD_PHASE =
    INSTANCE_CULLING_AND_LOD_FIRST_PHASE_BIT |
    INSTANCE_CULLING_AND_LOD_SECOND_PHASE_BIT;

struct InstanceCullingAndLODArgs {
  GLSL_PTR(Mesh) meshes;
  GLSL_PTR(mat4x3) transform_matrices;
  GLSL_PTR(InstanceCullData) cull_data;
  GLSL_PTR(DispatchIndirectCommand) meshlet_bucket_commands;
  GLSL_PTR(uint) meshlet_bucket_offsets;
  GLSL_PTR(uint) meshlet_bucket_sizes;
  GLSL_PTR(MeshletCullData) meshlet_cull_data;
  GLSL_PTR(GLSL_MESH_INSTANCE_VISIBILITY_MASK) mesh_instance_visibility;
  uint feature_mask;
  uint num_instances;
  mat4 proj_view;
  float lod_triangle_density;
  int lod_bias;
  SampledTexture2D hi_z;
};

GLSL_NAMESPACE_END

#endif // REN_GLSL_INSTANCE_CULLING_AND_LOD_H
