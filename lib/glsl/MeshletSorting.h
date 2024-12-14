#ifndef REN_GLSL_MESHLET_SORTING_H
#define REN_GLSL_MESHLET_SORTING_H

#include "Batch.h"
#include "Culling.h"
#include "DevicePtr.h"
#include "Std.h"

GLSL_NAMESPACE_BEGIN

GLSL_PUSH_CONSTANTS MeshletSortingArgs {
  GLSL_READONLY GLSL_PTR(uint) num_commands;
  GLSL_PTR(uint) batch_out_offsets;
  GLSL_READONLY GLSL_PTR(MeshletDrawCommand) unsorted_commands;
  GLSL_READONLY GLSL_PTR(glsl_BatchId) unsorted_command_batch_ids;
  GLSL_WRITEONLY GLSL_PTR(MeshletDrawCommand) commands;
}
GLSL_PC;

GLSL_NAMESPACE_END

#endif // REN_GLSL_MESHLET_SORTING_H
