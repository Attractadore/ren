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
  StableVector<VulkanCommandBuffer> m_frame_cmd_buffers;
  VkSemaphore m_frame_semaphore;

private:
  VulkanCommandPool &getFrameCommandPool();

  void waitForFrame(uint64_t frame) override;
  void beginFrameImpl() override;
  void endFrameImpl() override;

public:
  VulkanCommandAllocator(VulkanDevice *device, uint64_t pipeline_depth);
  ~VulkanCommandAllocator();

  VulkanDevice *getVulkanDevice() { return m_device; }

  VulkanCommandBuffer *allocateVulkanCommandBuffer();
  CommandBuffer *allocateCommandBuffer() override;
};
} // namespace ren
