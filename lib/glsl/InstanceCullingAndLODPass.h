#ifndef REN_GLSL_INSTANCE_CULLING_AND_LOD_H
#define REN_GLSL_INSTANCE_CULLING_AND_LOD_H

#include "Common.h"
#include "Culling.h"
#include "Indirect.h"
#include "Mesh.h"

GLSL_NAMESPACE_BEGIN

const uint INSTANCE_CULLING_AND_LOD_THREADS = 128;

const uint INSTANCE_CULLING_AND_LOD_FRUSTUM_BIT = 1 << 0;
const uint INSTANCE_CULLING_AND_LOD_LOD_SELECTION_BIT = 1 << 1;

struct InstanceCullingAndLODPassArgs {
  GLSL_REF(MeshRef) meshes;
  GLSL_REF(MeshInstanceRef) mesh_instances;
  GLSL_REF(TransformMatrixRef) transform_matrices;
  GLSL_REF(MeshletCullingDataRef) meshlet_cull_data;
  GLSL_REF(MeshletBucketDataOffsetRef) meshlet_bucket_offsets;
  GLSL_REF(MeshletBucketDataCountRef) meshlet_bucket_counts;
  GLSL_REF(DispatchIndirectCommandRef) meshlet_bucket_commands;
  uint feature_mask;
  uint num_mesh_instances;
  mat4 proj_view;
  float lod_triangle_density;
  int lod_bias;
};

GLSL_NAMESPACE_END

#endif // REN_GLSL_INSTANCE_CULLING_AND_LOD_H
