#ifndef REN_GLSL_MESHLET_SORTING_H
#define REN_GLSL_MESHLET_SORTING_H

#include "Batch.h"
#include "Common.h"
#include "Culling.h"
#include "DevicePtr.h"

GLSL_NAMESPACE_BEGIN

const uint MESHLET_SORTING_THREADS = 128;

struct MeshletSortingArgs {
  GLSL_PTR(uint) num_commands;
  GLSL_PTR(uint) batch_out_offsets;
  GLSL_PTR(MeshletDrawCommand) unsorted_commands;
  GLSL_PTR(BatchId) unsorted_command_batch_ids;
  GLSL_PTR(MeshletDrawCommand) commands;
  int pad;
};

GLSL_NAMESPACE_END

#endif // REN_GLSL_MESHLET_SORTING_H
