#ifndef REN_GLSL_PREPARE_INDIRECT_DRAW_H
#define REN_GLSL_PREPARE_INDIRECT_DRAW_H

#include "Culling.h"
#include "Indirect.h"
#include "Std.h"

GLSL_NAMESPACE_BEGIN

GLSL_PUSH_CONSTANTS PrepareBatchArgs {
  GLSL_READONLY GLSL_PTR(uint) batch_offset;
  GLSL_READONLY GLSL_PTR(uint) batch_size;
  GLSL_READONLY GLSL_PTR(MeshletDrawCommand) command_descs;
  GLSL_WRITEONLY GLSL_PTR(DrawIndexedIndirectCommand) commands;
}
GLSL_PC;

GLSL_NAMESPACE_END

#endif // REN_GLSL_PREPARE_INDIRECT_DRAW_H
