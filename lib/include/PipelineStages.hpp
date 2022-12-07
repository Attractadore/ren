#pragma once
#include "Support/Enum.hpp"

namespace ren {
#define REN_PIPELINE_STAGES (ColorOutput)(Present)(Compute)(Blit)
REN_DEFINE_FLAGS_ENUM(PipelineStage, REN_PIPELINE_STAGES);

#define REN_MEMORY_ACCESSES                                                    \
  (ColorWrite)(StorageRead)(StorageWrite)(TransferRead)(TransferWrite)
REN_DEFINE_FLAGS_ENUM(MemoryAccess, REN_MEMORY_ACCESSES);
}; // namespace ren
