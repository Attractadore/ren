#ifndef REN_GLSL_INDIRECT_H
#define REN_GLSL_INDIRECT_H

#include "BufferReference.h"

GLSL_NAMESPACE_BEGIN

struct DrawIndirectCommand {
  uint32_t num_vertices;
  uint32_t num_instances;
  uint32_t base_vertex;
  uint32_t base_instance;
};

struct DrawIndexedIndirectCommand {
  uint32_t num_indices;
  uint32_t num_instances;
  uint32_t base_index;
  uint32_t base_vertex;
  uint32_t base_instance;
};

struct DispatchIndirectCommand {
  uint32_t x;
  uint32_t y;
  uint32_t z;
};

GLSL_REF_TYPE(4) DrawIndexedIndirectCommandRef {
  DrawIndexedIndirectCommand command;
};

GLSL_REF_TYPE(4) DispatchIndirectCommandRef {
  DispatchIndirectCommand command;
};

GLSL_REF_TYPE(4) IndirectCommandCountRef { uint count; };

GLSL_NAMESPACE_END

#endif // REN_GLSL_INDIRECT_H
