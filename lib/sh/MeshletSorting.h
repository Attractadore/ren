#pragma once
#include "Geometry.h"
#include "Std.h"

namespace ren::sh {

static const uint MESHLET_SORTING_THREADS = 128;

struct MeshletSortingArgs {
  DevicePtr<uint> num_commands;
  DevicePtr<uint> batch_out_offsets;
  DevicePtr<MeshletDrawCommand> unsorted_commands;
  DevicePtr<BatchId> unsorted_command_batch_ids;
  DevicePtr<MeshletDrawCommand> commands;
};

} // namespace ren::sh
