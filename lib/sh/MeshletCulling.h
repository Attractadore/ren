#pragma once
#include "Geometry.h"
#include "Std.h"

namespace ren::sh {

static const uint NUM_MESHLET_CULLING_BUCKETS = MESH_MESHLET_COUNT_BITS;

static const uint MESHLET_CULLING_THREADS = 128;

static const uint MESHLET_CULLING_CONE_BIT = 1 << 0;
static const uint MESHLET_CULLING_FRUSTUM_BIT = 1 << 1;
static const uint MESHLET_CULLING_OCCLUSION_BIT = 1 << 2;

struct MeshletCullingArgs {
  DevicePtr<Mesh> meshes;
  DevicePtr<mat4x3> transform_matrices;
  /// Pointer to current bucket's cull data.
  DevicePtr<MeshletCullData> bucket_cull_data;
  /// Pointer to current bucket's size.
  DevicePtr<uint> bucket_size;
  DevicePtr<uint> batch_sizes;
  DevicePtr<DispatchIndirectCommand> batch_prepare_commands;
  DevicePtr<MeshletDrawCommand> commands;
  DevicePtr<BatchId> command_batch_ids;
  DevicePtr<uint> num_commands;
  DevicePtr<DispatchIndirectCommand> sort_command;
  mat4 proj_view;
  vec3 eye;
  uint feature_mask;
  /// Current bucket index.
  uint bucket;
  Handle<Sampler2D> hi_z;
};

} // namespace ren::sh
