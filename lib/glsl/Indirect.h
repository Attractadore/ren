#ifndef REN_GLSL_INDIRECT_H
#define REN_GLSL_INDIRECT_H

#include "common.h"

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

GLSL_BUFFER(4) DrawIndexedIndirectCommands {
  DrawIndexedIndirectCommand command;
};

GLSL_NAMESPACE_END

#endif // REN_GLSL_INDIRECT_H
