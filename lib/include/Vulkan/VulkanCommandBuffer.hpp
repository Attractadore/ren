#pragma once
#include "CommandBuffer.hpp"
#include "Support/Enum.hpp"
#include "Support/Vector.hpp"
#include "VulkanSync.hpp"

namespace ren {
namespace detail {
constexpr auto render_target_load_op_map = std::array{
    std::pair(RenderTargetLoadOp::Clear, VK_ATTACHMENT_LOAD_OP_CLEAR),
    std::pair(RenderTargetLoadOp::Load, VK_ATTACHMENT_LOAD_OP_LOAD),
    std::pair(RenderTargetLoadOp::Discard, VK_ATTACHMENT_LOAD_OP_DONT_CARE),
};

constexpr auto render_target_store_op_map = std::array{
    std::pair(RenderTargetStoreOp::Store, VK_ATTACHMENT_STORE_OP_STORE),
    std::pair(RenderTargetStoreOp::Discard, VK_ATTACHMENT_STORE_OP_DONT_CARE),
};
} // namespace detail

constexpr auto getVkAttachmentLoadOp =
    enumMap<detail::render_target_load_op_map>;
constexpr auto getRenderTargetLoadOp =
    inverseEnumMap<detail::render_target_load_op_map>;

constexpr auto getVkAttachmentStoreOp =
    enumMap<detail::render_target_store_op_map>;
constexpr auto getRenderTargetStoreOp =
    inverseEnumMap<detail::render_target_store_op_map>;

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
