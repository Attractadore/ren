#pragma once
#include "Geometry.h"
#include "Std.h"

namespace ren::sh {

struct EarlyZArgs {
  DevicePtr<Mesh> meshes;
  DevicePtr<MeshInstance> mesh_instances;
  DevicePtr<mat4x3> transform_matrices;
  mat4 proj_view;
};

} // namespace ren::sh
