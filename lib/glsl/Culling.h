#ifndef REN_GLSL_CULLING_H
#define REN_GLSL_CULLING_H

#include "Common.h"
#include "DevicePtr.h"
#include "Mesh.h"

GLSL_NAMESPACE_BEGIN

struct InstanceCullData {
  uint mesh;
  uint mesh_instance;
};

GLSL_DEFINE_PTR_TYPE(InstanceCullData, 4);

struct MeshletCullData {
  uint mesh;
  uint mesh_instance;
  uint base_meshlet;
};

GLSL_DEFINE_PTR_TYPE(MeshletCullData, 4);

const uint MESHLET_CULLING_THREADS = 128;

const uint NUM_MESHLET_CULLING_BUCKETS = MESH_MESHLET_COUNT_BITS;

GLSL_NAMESPACE_END

#endif // REN_GLSL_CULLING_H
