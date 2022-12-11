#include "Vulkan/VulkanCommandBuffer.hpp"
#include "Vulkan/VulkanCommandAllocator.hpp"
#include "Vulkan/VulkanDevice.hpp"
#include "Vulkan/VulkanPipelineStages.hpp"
#include "Vulkan/VulkanTexture.hpp"

#include <range/v3/view.hpp>

namespace ren {
VulkanCommandBuffer::VulkanCommandBuffer(VulkanDevice *device,
                                         VkCommandBuffer cmd_buffer,
                                         VulkanCommandAllocator *parent)
    : m_device(device), m_cmd_buffer(cmd_buffer), m_parent(parent) {
  VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  m_device->BeginCommandBuffer(m_cmd_buffer, &begin_info);
}

void VulkanCommandBuffer::beginRendering(
    int x, int y, unsigned width, unsigned height,
    SmallVector<RenderTargetConfig, 8> render_targets,
    std::optional<DepthStencilTargetConfig> depth_stencil_target) {

  auto color_attachments =
      render_targets |
      ranges::views::transform([&](const RenderTargetConfig &rt) {
        const auto &cc = rt.clear_color;
        return VkRenderingAttachmentInfo{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = (rt.load_op != TargetLoadOp::None)
                             ? m_device->getVkImageView(rt.rtv)
                             : VK_NULL_HANDLE,
            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .loadOp = getVkAttachmentLoadOp(rt.load_op),
            .storeOp = getVkAttachmentStoreOp(rt.store_op),
            .clearValue = {.color = {.float32 = {cc[0], cc[1], cc[2], cc[3]}}},
        };
      }) |
      ranges::to<SmallVector<VkRenderingAttachmentInfo, 8>>;
  for (auto &rt : render_targets) {
    m_parent->addFrameResource(std::move(rt.rtv));
  }

  VkRenderingAttachmentInfo depth_attachment = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  VkRenderingAttachmentInfo stencil_attachment = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};

  if (depth_stencil_target) {
    auto &dst = *depth_stencil_target;
    auto view = m_device->getVkImageView(dst.dsv);
    depth_attachment = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView =
            (dst.depth_load_op != TargetLoadOp::None) ? view : VK_NULL_HANDLE,
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp = getVkAttachmentLoadOp(dst.depth_load_op),
        .storeOp = getVkAttachmentStoreOp(dst.depth_store_op),
        .clearValue = {.depthStencil = {.depth = dst.clear_depth}},
    };
    stencil_attachment = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView =
            (dst.stencil_load_op != TargetLoadOp::None) ? view : VK_NULL_HANDLE,
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp = getVkAttachmentLoadOp(dst.stencil_load_op),
        .storeOp = getVkAttachmentStoreOp(dst.stencil_store_op),
        .clearValue = {.depthStencil = {.stencil = dst.clear_stencil}},
    };
    m_parent->addFrameResource(std::move(depth_stencil_target->dsv));
  }

  VkRenderingInfo rendering_info = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = {.offset = {x, y}, .extent = {width, height}},
      .layerCount = 1,
      .colorAttachmentCount = static_cast<uint32_t>(color_attachments.size()),
      .pColorAttachments = color_attachments.data(),
      .pDepthAttachment = &depth_attachment,
      .pStencilAttachment = &stencil_attachment,
  };

  m_device->CmdBeginRendering(m_cmd_buffer, &rendering_info);
}

void VulkanCommandBuffer::endRendering() {
  m_device->CmdEndRendering(m_cmd_buffer);
}

void VulkanCommandBuffer::blit(Texture src, Texture dst,
                               std::span<const VkImageBlit> regions,
                               VkFilter filter) {
  auto vk_src = getVkImage(src);
  auto vk_dst = getVkImage(dst);
  m_parent->addFrameResource(std::move(src));
  m_parent->addFrameResource(std::move(dst));
  m_device->CmdBlitImage(m_cmd_buffer, vk_src,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk_dst,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, regions.size(),
                         regions.data(), filter);
}

namespace {
template <typename Vec>
void addSemaphore(VulkanCommandAllocator *parent, Vec &semaphores,
                  SyncObject sync, PipelineStageFlags stages) {
  assert(sync.desc.type == SyncType::Semaphore);
  semaphores.push_back({
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = getVkSemaphore(sync),
      .stageMask = getVkPipelineStageFlags(stages),
  });
  parent->addFrameResource(std::move(sync));
}
} // namespace

void VulkanCommandBuffer::wait(SyncObject sync, PipelineStageFlags stages) {
  addSemaphore(m_parent, m_wait_semaphores, std::move(sync), stages);
}

void VulkanCommandBuffer::signal(SyncObject sync, PipelineStageFlags stages) {
  addSemaphore(m_parent, m_signal_semaphores, std::move(sync), stages);
}

void VulkanCommandBuffer::close() {
  throwIfFailed(m_device->EndCommandBuffer(m_cmd_buffer),
                "Vulkan: Failed to record command buffer");
}
} // namespace ren
