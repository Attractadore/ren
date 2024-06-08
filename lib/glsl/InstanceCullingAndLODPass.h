#ifndef REN_GLSL_INSTANCE_CULLING_AND_LOD_H
#define REN_GLSL_INSTANCE_CULLING_AND_LOD_H

#include "Common.h"
#include "DevicePtr.h"
#include "Indirect.h"
#include "Mesh.h"

GLSL_NAMESPACE_BEGIN

const uint INSTANCE_CULLING_AND_LOD_THREADS = 128;

const uint INSTANCE_CULLING_AND_LOD_FRUSTUM_BIT = 1 << 0;
const uint INSTANCE_CULLING_AND_LOD_LOD_SELECTION_BIT = 1 << 1;

struct InstanceCullingAndLODPassArgs {
  GLSL_PTR(Mesh) meshes;
  GLSL_PTR(MeshInstance) mesh_instances;
  GLSL_PTR(mat4x3) transform_matrices;
  GLSL_PTR(uint) batch_command_offsets;
  GLSL_PTR(uint) batch_command_counts;
  GLSL_PTR(DrawIndexedIndirectCommand) commands;
  uint feature_mask;
  uint num_mesh_instances;
  mat4 proj_view;
  float lod_triangle_density;
  int lod_bias;
};

GLSL_NAMESPACE_END

#endif // REN_GLSL_INSTANCE_CULLING_AND_LOD_H
