#pragma once
#include "CommandBuffer.hpp"
#include "Support/Vector.hpp"
#include "VulkanSync.hpp"

#include <vulkan/vulkan.h>

namespace ren {
class VulkanDevice;
class VulkanCommandAllocator;

class VulkanCommandBuffer final : public CommandBuffer {
  VulkanDevice *m_device;
  VkCommandBuffer m_cmd_buffer;
  VulkanCommandAllocator *m_parent;
  SmallVector<VkSemaphoreSubmitInfo> m_wait_semaphores;
  SmallVector<VkSemaphoreSubmitInfo> m_signal_semaphores;

public:
  VulkanCommandBuffer(VulkanDevice *device, VkCommandBuffer cmd_buffer,
                      VulkanCommandAllocator *parent);

  void beginRendering(
      int x, int y, unsigned width, unsigned height,
      SmallVector<RenderTargetConfig, 8> render_targets,
      std::optional<DepthRenderTargetConfig> depth_render_target,
      std::optional<StencilRenderTargetConfig> stencil_render_target) override;
  void endRendering() override;

  void blit(Texture src, Texture dst, std::span<const BlitRegion> regions,
            Filter filter) override;
  using CommandBuffer::blit;

  void wait(SyncObject sync, PipelineStageFlags stages) override;
  void signal(SyncObject sync, PipelineStageFlags stages) override;
  std::span<const VkSemaphoreSubmitInfo> getWaitSemaphores() const {
    return m_wait_semaphores;
  }
  std::span<const VkSemaphoreSubmitInfo> getSignalSemaphores() const {
    return m_signal_semaphores;
  }

  void close() override;

  VulkanDevice *getDevice() { return m_device; }
  VkCommandBuffer get() { return m_cmd_buffer; }
};
} // namespace ren
