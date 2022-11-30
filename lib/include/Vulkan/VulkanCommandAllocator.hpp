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
  SmallVector<Vector<AnyRef>, 3> m_frame_resource_lists;
  VkSemaphore m_frame_semaphore;
  uint64_t m_frame_time;
  // For indexing frame data
  unsigned m_frame_index = 0;

private:
  VulkanCommandPool &getFrameCommandPool();
  Vector<AnyRef> &getFrameResourceList();

public:
  VulkanCommandAllocator(VulkanDevice *device, unsigned pipeline_depth);
  ~VulkanCommandAllocator();

  unsigned getPipelineDepth() const override;

  VulkanDevice *getVulkanDevice() { return m_device; }

  VulkanCommandBuffer *allocateVulkanCommandBuffer();
  CommandBuffer *allocateCommandBuffer() override;

  void beginFrame() override;
  void endFrame() override;

  void addFrameResource(AnyRef resoure);

  void addFrameResource(Texture texture) {
    addFrameResource(std::move(texture.handle));
  }

  void addFrameResource(TextureView view) {
    addFrameResource(std::move(view.texture));
  }

  void addFrameResource(SyncObject sync) {
    addFrameResource(std::move(sync.handle));
  }
};
} // namespace ren
