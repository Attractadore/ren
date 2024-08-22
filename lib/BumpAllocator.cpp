#include "BumpAllocator.hpp"
#include "CommandRecorder.hpp"

namespace ren {

void DeviceBumpAllocator::reset(CommandRecorder &rec) {
  Base::reset();
  rec.pipeline_barrier({{
                           .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                           .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                           .srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT |
                                            VK_ACCESS_2_MEMORY_WRITE_BIT,
                           .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                           .dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
                       }},
                       {});
}

} // namespace ren
