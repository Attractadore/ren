#ifndef REN_GLSL_MESHLET_CULLING_PASS_H
#define REN_GLSL_MESHLET_CULLING_PASS_H

#include "Common.h"
#include "Culling.h"
#include "Indirect.h"
#include "Mesh.h"

GLSL_NAMESPACE_BEGIN

struct MeshletCullingPassArgs {
  GLSL_PTR(Mesh) meshes;
  /// Pointer to current bucket's cull data.
  GLSL_PTR(MeshletCullData) bucket_cull_data;
  /// Pointer to current bucket's size.
  GLSL_PTR(uint) bucket_size;
  GLSL_PTR(DrawIndexedIndirectCommand) commands;
  GLSL_PTR(uint) num_commands;
  /// Current bucket index.
  uint bucket;
};

GLSL_NAMESPACE_END

#endif // REN_GLSL_MESHLET_CULLING_PASS_H
