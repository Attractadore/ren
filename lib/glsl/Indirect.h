#ifndef REN_GLSL_INDIRECT_H
#define REN_GLSL_INDIRECT_H

#include "Common.h"
#include "DevicePtr.h"

GLSL_NAMESPACE_BEGIN

struct DrawIndirectCommand {
  uint32_t num_vertices;
  uint32_t num_instances;
  uint32_t base_vertex;
  uint32_t base_instance;
};

GLSL_DEFINE_PTR_TYPE(DrawIndirectCommand, 4);

struct DrawIndexedIndirectCommand {
  uint32_t num_indices;
  uint32_t num_instances;
  uint32_t base_index;
  uint32_t base_vertex;
  uint32_t base_instance;
};

GLSL_DEFINE_PTR_TYPE(DrawIndexedIndirectCommand, 4);

struct DispatchIndirectCommand {
  uint32_t x;
  uint32_t y;
  uint32_t z;
};

GLSL_DEFINE_PTR_TYPE(DispatchIndirectCommand, 4);

GLSL_NAMESPACE_END

#endif // REN_GLSL_INDIRECT_H
