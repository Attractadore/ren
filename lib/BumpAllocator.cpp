#include "BumpAllocator.hpp"
#include "CommandRecorder.hpp"

namespace ren {

void DeviceBumpAllocator::reset(CommandRecorder &rec) {
  Base::reset();
  rec.memory_barrier({
      .src_stage_mask = rhi::PipelineStage::All,
      .src_access_mask = rhi::Access::MemoryRead | rhi::Access::MemoryWrite,
      .dst_stage_mask = rhi::PipelineStage::All,
      .dst_access_mask = rhi::Access::MemoryWrite,
  });
}

} // namespace ren
