#pragma once
#include "Support/Flags.hpp"

namespace ren {
enum class PipelineStage {
  ColorOutput = 1 << 0,
  Blit = 1 << 1,
  Present = 1 << 2,
  Compute = 1 << 3,
};

ENABLE_FLAGS(PipelineStage);

enum class MemoryAccess {
  ColorWrite = 1 << 0,
  TransferRead = 1 << 1,
  TransferWrite = 1 << 2,
  StorageRead = 1 << 3,
  StorageWrite = 1 << 4,
};

ENABLE_FLAGS(MemoryAccess);
}; // namespace ren
