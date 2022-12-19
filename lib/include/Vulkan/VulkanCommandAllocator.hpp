#pragma once
#include "CommandAllocator.hpp"
#include "Support/StableVector.hpp"
#include "VulkanCommandBuffer.hpp"
#include "VulkanCommandPool.hpp"

namespace ren {
class VulkanDevice;
class VulkanCommandBuffer;

class VulkanCommandAllocator final : public CommandAllocator {
  VulkanDevice *m_device;
  SmallVector<VulkanCommandPool, 3> m_frame_pools;
  SmallVector<uint64_t, 3> m_frame_times;
  StableVector<VulkanCommandBuffer> m_frame_cmd_buffers;
  unsigned m_frame_index = 0;

private:
  void beginFrameImpl() override;
  void endFrameImpl() override;

public:
  VulkanCommandAllocator(VulkanDevice &device, unsigned pipeline_depth);

  unsigned getPipelineDepth() const override;

  VulkanDevice *getVulkanDevice() { return m_device; }

  VulkanCommandBuffer *allocateVulkanCommandBuffer();
  CommandBuffer *allocateCommandBuffer() override;
};
} // namespace ren
