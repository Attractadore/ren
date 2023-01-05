#pragma once
#include "Pipeline.hpp"

#include <vulkan/vulkan.h>

namespace ren {

inline VkPipeline getVkPipeline(const PipelineLike auto &pipeline) {
  return reinterpret_cast<VkPipeline>(pipeline.get());
}

inline VkPipelineLayout
getVkPipelineLayout(const PipelineSignatureLike auto &signature) {
  return reinterpret_cast<VkPipelineLayout>(signature.get());
}

} // namespace ren
