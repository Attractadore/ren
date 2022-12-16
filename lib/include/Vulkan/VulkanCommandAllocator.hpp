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
  unsigned m_frame_number;
  unsigned m_frame_index;

private:
  VulkanCommandPool &getFrameCommandPool();
  Vector<AnyRef> &getFrameResourceList();

  void waitForFrame(uint64_t frame);
  void beginFrameImpl() override;
  void endFrameImpl() override;

public:
  VulkanCommandAllocator(VulkanDevice *device, unsigned pipeline_depth);
  ~VulkanCommandAllocator();

  unsigned getPipelineDepth() const override;

  VulkanDevice *getVulkanDevice() { return m_device; }

  VulkanCommandBuffer *allocateVulkanCommandBuffer();
  CommandBuffer *allocateCommandBuffer() override;

  void addFrameResource(AnyRef resoure);

  void addFrameResource(Texture texture) {
    addFrameResource(std::move(texture.handle));
  }

  void addFrameResource(RenderTargetView rtv) {
    addFrameResource(std::move(rtv.texture));
  }

  void addFrameResource(DepthStencilView dsv) {
    addFrameResource(std::move(dsv.texture));
  }

  void addFrameResource(SampledTextureView stv) {
    addFrameResource(std::move(stv.texture));
  }

  void addFrameResource(StorageTextureView stv) {
    addFrameResource(std::move(stv.texture));
  }

  void addFrameResource(SyncObject sync) {
    addFrameResource(std::move(sync.handle));
  }
};
} // namespace ren
