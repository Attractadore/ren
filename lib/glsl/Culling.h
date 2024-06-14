#ifndef REN_GLSL_CULLING_H
#define REN_GLSL_CULLING_H

#include "DevicePtr.h"

GLSL_NAMESPACE_BEGIN

struct InstanceCullData {
  uint mesh;
  uint mesh_instance;
};

GLSL_DEFINE_PTR_TYPE(InstanceCullData, 4);

GLSL_NAMESPACE_END

#endif // REN_GLSL_CULLING_H
