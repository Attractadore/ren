#pragma once
#include "Geometry.h"
#include "Std.h"

namespace ren::sh {

// Phase 1: Reject not visible in previous frame. Perform culling (without
// occlusion check) and LOD selection. Draw.
// Phase 2: Perform culling (with occlusion check). Generate new visibility
// buffer. Select LOD and draw if not visible in previous frame.
// Phase 3+: Reject not visible in current frame. Select LOD. Draw.

static const uint INSTANCE_CULLING_AND_LOD_LOD_SELECTION_BIT = 1 << 0;
static const uint INSTANCE_CULLING_AND_LOD_FRUSTUM_BIT = 1 << 1;
static const uint INSTANCE_CULLING_AND_LOD_OCCLUSION_BIT = 1 << 2;

static const uint INSTANCE_CULLING_AND_LOD_FIRST_PHASE_BIT = 1 << 3;
static const uint INSTANCE_CULLING_AND_LOD_SECOND_PHASE_BIT = 1 << 4;

struct InstanceCullingAndLODArgs {
  DevicePtr<Mesh> meshes;
  DevicePtr<mat4x3> transform_matrices;
  DevicePtr<DrawSetItem> ds;
  DevicePtr<DispatchIndirectCommand> meshlet_bucket_commands;
  // These can't be push constants because they are indexed dynamically.
  SH_RG_IGNORE(DevicePtr<uint>) meshlet_bucket_offsets;
  DevicePtr<uint> meshlet_bucket_sizes;
  DevicePtr<MeshletCullData> meshlet_cull_data;
  DevicePtr<MeshInstanceVisibilityMask> mesh_instance_visibility;
  uint feature_mask;
  uint num_instances;
  mat4 proj_view;
  float lod_triangle_density;
  int lod_bias;
  Handle<Sampler2D> hi_z;
};

} // namespace ren::sh
