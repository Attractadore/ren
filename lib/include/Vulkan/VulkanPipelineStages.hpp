#pragma once
#include "PipelineStages.hpp"
#include "Support/Enum.hpp"

#include <vulkan/vulkan.h>

namespace ren {
namespace detail {
inline constexpr std::array pipeline_stage_map = {
    std::pair(PipelineStage::ColorOutput,
              VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT),
    std::pair(PipelineStage::Blit, VK_PIPELINE_STAGE_2_BLIT_BIT),
    std::pair(PipelineStage::Compute, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT),
};

inline constexpr std::array memory_access_map = {
    std::pair(MemoryAccess::ColorWrite, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT),
    std::pair(MemoryAccess::TransferRead, VK_ACCESS_2_TRANSFER_READ_BIT),
    std::pair(MemoryAccess::TransferWrite, VK_ACCESS_2_TRANSFER_WRITE_BIT),
    std::pair(MemoryAccess::StorageRead, VK_ACCESS_2_SHADER_STORAGE_READ_BIT),
    std::pair(MemoryAccess::StorageWrite, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT),
};
} // namespace detail

inline constexpr auto getVkPipelineStageFlags =
    flagsMap<detail::pipeline_stage_map>;

inline constexpr auto getVkAccessFlags = flagsMap<detail::memory_access_map>;
} // namespace ren
