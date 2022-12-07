#pragma once
#include "CommandBuffer.hpp"
#include "Support/Vector.hpp"
#include "VulkanSync.hpp"

namespace ren {
REN_MAP_TYPE(RenderTargetLoadOp, VkAttachmentLoadOp);
REN_MAP_FIELD(RenderTargetLoadOp::Clear, VK_ATTACHMENT_LOAD_OP_CLEAR);
REN_MAP_FIELD(RenderTargetLoadOp::Load, VK_ATTACHMENT_LOAD_OP_LOAD);
REN_MAP_FIELD(RenderTargetLoadOp::Discard, VK_ATTACHMENT_LOAD_OP_DONT_CARE);
REN_MAP_ENUM(getVkAttachmentLoadOp, RenderTargetLoadOp,
             REN_RENDER_TARGET_LOAD_OPS);

REN_MAP_TYPE(RenderTargetStoreOp, VkAttachmentStoreOp);
REN_MAP_FIELD(RenderTargetStoreOp::Store, VK_ATTACHMENT_STORE_OP_STORE);
REN_MAP_FIELD(RenderTargetStoreOp::Discard, VK_ATTACHMENT_STORE_OP_DONT_CARE);
REN_MAP_ENUM(getVkAttachmentStoreOp, RenderTargetStoreOp,
             REN_RENDER_TARGET_STORE_OPS);

REN_MAP_TYPE(Filter, VkFilter);
REN_MAP_FIELD(Filter::Nearest, VK_FILTER_NEAREST);
REN_MAP_FIELD(Filter::Linear, VK_FILTER_LINEAR);
REN_MAP_ENUM(getVkFilter, Filter, REN_FILTERS);

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
