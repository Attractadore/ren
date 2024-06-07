#ifndef REN_GLSL_MESHLET_CULLING_PASS_H
#define REN_GLSL_MESHLET_CULLING_PASS_H

#include "Common.h"
#include "Culling.h"
#include "Indirect.h"
#include "Mesh.h"

GLSL_NAMESPACE_BEGIN

struct MeshletCullingPassArgs {
  GLSL_REF(MeshRef) meshes;
  GLSL_REF(MeshletCullingDataRef) meshlet_cull_data;
  GLSL_REF(MeshletBucketDataOffsetRef) meshlet_bucket_offsets;
  GLSL_REF(MeshletBucketDataCountRef) meshlet_bucket_counts;
  GLSL_REF(DrawIndexedIndirectCommand) commands;
  GLSL_REF(IndirectCommandCountRef) command_count;
  uint bucket;
};

GLSL_NAMESPACE_END

#endif // REN_GLSL_MESHLET_CULLING_PASS_H
