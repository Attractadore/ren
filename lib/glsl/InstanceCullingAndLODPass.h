#ifndef REN_GLSL_INSTANCE_CULLING_AND_LOD_H
#define REN_GLSL_INSTANCE_CULLING_AND_LOD_H

#include "Array.h"
#include "Common.h"
#include "Culling.h"
#include "DevicePtr.h"
#include "Indirect.h"
#include "Mesh.h"

GLSL_NAMESPACE_BEGIN

const uint INSTANCE_CULLING_AND_LOD_THREADS = 128;

const uint INSTANCE_CULLING_AND_LOD_FRUSTUM_BIT = 1 << 0;
const uint INSTANCE_CULLING_AND_LOD_LOD_SELECTION_BIT = 1 << 1;

struct InstanceCullingAndLODPassUniforms {
  uint feature_mask;
  uint num_instances;
  mat4 proj_view;
  float lod_triangle_density;
  int lod_bias;
  GLSL_ARRAY(uint, meshlet_bucket_offsets, NUM_MESHLET_CULLING_BUCKETS);
};

GLSL_DEFINE_PTR_TYPE(InstanceCullingAndLODPassUniforms, 4);

struct InstanceCullingAndLODPassArgs {
  GLSL_PTR(InstanceCullingAndLODPassUniforms) ub;
  GLSL_PTR(Mesh) meshes;
  GLSL_PTR(mat4x3) transform_matrices;
  GLSL_PTR(InstanceCullData) cull_data;
  GLSL_PTR(DispatchIndirectCommand) meshlet_bucket_commands;
  GLSL_PTR(uint) meshlet_bucket_sizes;
  GLSL_PTR(MeshletCullData) meshlet_cull_data;
  int pad;
};

GLSL_NAMESPACE_END

#endif // REN_GLSL_INSTANCE_CULLING_AND_LOD_H
