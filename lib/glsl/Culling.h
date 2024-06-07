#ifndef REN_GLSL_CULLING_H
#define REN_GLSL_CULLING_H

#include "Common.h"

GLSL_NAMESPACE_BEGIN

struct MeshletCullingData {
  uint mesh;
  uint mesh_instance;
  uint base_meshlet;
};

GLSL_REF_TYPE(4) MeshletCullingDataRef { MeshletCullingData data; };

GLSL_REF_TYPE(4) MeshletBucketDataOffsetRef { uint offset; };

GLSL_REF_TYPE(4) MeshletBucketDataCountRef { uint count; };

const uint MESHLET_CULLING_THREADS = 128;

GLSL_NAMESPACE_END

#endif // REN_GLSL_CULLING_H
