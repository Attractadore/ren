#ifndef REN_GLSL_PREPARE_INDIRECT_DRAW_H
#define REN_GLSL_PREPARE_INDIRECT_DRAW_H

#include "Common.h"
#include "Culling.h"
#include "Indirect.h"

GLSL_NAMESPACE_BEGIN

const uint PREPARE_BATCH_THREADS = 128;

struct PrepareBatchArgs {
  GLSL_PTR(uint) batch_offset;
  GLSL_PTR(uint) batch_size;
  GLSL_PTR(MeshletDrawCommand) command_descs;
  GLSL_PTR(DrawIndexedIndirectCommand) commands;
  int pad;
};

GLSL_NAMESPACE_END

#endif // REN_GLSL_PREPARE_INDIRECT_DRAW_H
