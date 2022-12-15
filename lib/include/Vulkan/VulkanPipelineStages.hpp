#pragma once
#include "PipelineStages.hpp"

#include <vulkan/vulkan.h>

namespace ren {
#define REN_VULKAN_PIPELINE_STAGES (ColorOutput)(Compute)(Blit)(FragmentShader)
REN_MAP_TYPE(PipelineStage, VkPipelineStageFlagBits2);
REN_MAP_FIELD(PipelineStage::ColorOutput,
              VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
REN_MAP_FIELD(PipelineStage::Blit, VK_PIPELINE_STAGE_2_BLIT_BIT);
REN_MAP_FIELD(PipelineStage::Compute, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
REN_MAP_FIELD(PipelineStage::FragmentShader,
              VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);

namespace detail {
REN_MAP_ENUM(getVkPipelineStage, PipelineStage, REN_VULKAN_PIPELINE_STAGES);
}
#undef REN_VULKAN_PIPELINE_STAGES

inline VkPipelineStageFlagBits2 getVkPipelineStage(PipelineStage stage) {
  switch (stage) {
  default:
    return detail::getVkPipelineStage(stage);
  case PipelineStage::Present:
    return {};
  }
}

constexpr auto getVkPipelineStageFlags =
    detail::mapFlags<PipelineStage, getVkPipelineStage>;

REN_MAP_TYPE(MemoryAccess, VkAccessFlagBits2);
REN_MAP_FIELD(MemoryAccess::ColorWrite, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
REN_MAP_FIELD(MemoryAccess::TransferRead, VK_ACCESS_2_TRANSFER_READ_BIT);
REN_MAP_FIELD(MemoryAccess::TransferWrite, VK_ACCESS_2_TRANSFER_WRITE_BIT);
REN_MAP_FIELD(MemoryAccess::StorageRead, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
REN_MAP_FIELD(MemoryAccess::StorageWrite, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
REN_MAP_FIELD(MemoryAccess::SampledRead, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

REN_MAP_ENUM_AND_FLAGS(getVkAccess, MemoryAccess, REN_MEMORY_ACCESSES);
} // namespace ren
