#pragma once
#include "Support/Enum.hpp"

namespace ren {
#define REN_PIPELINE_STAGES_WITHOUT_PRESENT                                    \
  (VertexShader)(ColorOutput)(FragmentShader)(ComputeShader)(Blit)
#define REN_PIPELINE_STAGES REN_PIPELINE_STAGES_WITHOUT_PRESENT(Present)
REN_DEFINE_FLAGS_ENUM(PipelineStage, REN_PIPELINE_STAGES);

#define REN_MEMORY_ACCESSES                                                    \
  (IndirectRead)(IndexRead)(                                                   \
      ColorWrite)(DepthRead)(DepthWrite)(UniformRead)(SampledRead)(StorageRead)(StorageWrite)(TransferRead)(TransferWrite)
REN_DEFINE_FLAGS_ENUM(MemoryAccess, REN_MEMORY_ACCESSES);
}; // namespace ren
