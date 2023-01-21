#pragma once
#include "CommandAllocator.hpp"
#include "CommandBuffer.hpp"
#include "Config.hpp"
#include "Support/StableVector.hpp"
#include "VulkanCommandPool.hpp"

namespace ren {
class VulkanDevice;

class VulkanCommandAllocator final : public CommandAllocator {
  VulkanDevice *m_device;
  StaticVector<VulkanCommandPool, c_pipeline_depth> m_frame_pools;
  StableVector<CommandBuffer> m_frame_cmd_buffers;
  unsigned m_frame_index = 0;

public:
  VulkanCommandAllocator(VulkanDevice &device);

  VulkanDevice *getVulkanDevice() { return m_device; }

  void begin_frame() override;
  void end_frame() override;

  CommandBuffer *allocateCommandBuffer() override;
};
} // namespace ren
