#pragma once
#include "Pipeline.hpp"

#include <vulkan/vulkan.h>

namespace ren {

inline VkPipeline getVkPipeline(PipelineRef pipeline) {
  return reinterpret_cast<VkPipeline>(pipeline.get());
}

inline VkPipelineLayout getVkPipelineLayout(PipelineSignatureRef signature) {
  return reinterpret_cast<VkPipelineLayout>(signature.get());
}

} // namespace ren
