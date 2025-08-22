#pragma once
#include "Geometry.h"
#include "Std.h"

namespace ren::sh {

static const uint PREPARE_BATCH_THREADS = 128;

struct PrepareBatchArgs {
  DevicePtr<uint> batch_offset;
  DevicePtr<uint> batch_size;
  DevicePtr<MeshletDrawCommand> command_descs;
  DevicePtr<DrawIndexedIndirectCommand> commands;
};

} // namespace ren::sh
