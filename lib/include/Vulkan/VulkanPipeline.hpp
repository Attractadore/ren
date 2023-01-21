#pragma once
#include "Pipeline.hpp"

namespace ren {

inline VkPipeline getVkPipeline(GraphicsPipelineRef pipeline) {
  return reinterpret_cast<VkPipeline>(pipeline.handle);
}

inline VkPipelineLayout getVkPipelineLayout(PipelineSignatureRef signature) {
  return reinterpret_cast<VkPipelineLayout>(signature.handle);
}

} // namespace ren
