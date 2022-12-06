#pragma once
#include "Support/Flags.hpp"

namespace ren {
// clang-format off
BEGIN_FLAGS_ENUM(PipelineStage) {
  FLAG(ColorOutput),
  FLAG(Present),
  FLAG(Compute),
  FLAG(Blit),
} END_FLAGS_ENUM(PipelineStage);
// clang-format on

// clang-format off
BEGIN_FLAGS_ENUM(MemoryAccess) {
  FLAG(ColorWrite),
  FLAG(StorageRead),
  FLAG(StorageWrite),
  FLAG(TransferRead),
  FLAG(TransferWrite),
} END_FLAGS_ENUM(MemoryAccess);
// clang-format on
}; // namespace ren
