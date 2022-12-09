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

namespace {
enum class TargetType {
  Color,
  Depth,
  Stencil,
};

template <TargetType type>
VkRenderingAttachmentInfo getVkRenderingAttachmentInfo(VulkanDevice *device,
                                                       const auto &cfg) {
  TargetLoadOp load_op;
  TargetStoreOp store_op;
  VkClearValue clear_value;
  VkImageView image_view = VK_NULL_HANDLE;
  if constexpr (type == TargetType::Color) {
    load_op = cfg.load_op;
    store_op = cfg.store_op;
    const auto &cc = cfg.clear_color;
    clear_value = {.color = {.float32 = {cc[0], cc[1], cc[2], cc[3]}}};
  } else if constexpr (type == TargetType::Depth) {
    load_op = cfg.depth_load_op;
    store_op = cfg.depth_store_op;
    clear_value = {.depthStencil = {.depth = cfg.clear_depth}};
  } else if constexpr (type == TargetType::Stencil) {
    load_op = cfg.stencil_load_op;
    store_op = cfg.stencil_store_op;
    clear_value = {.depthStencil = {.stencil = cfg.clear_stencil}};
  }
  if (load_op != TargetLoadOp::None or store_op != TargetStoreOp::None) {
    image_view = device->getVkImageView([&] {
      if constexpr (type == TargetType::Color) {
        return cfg.rtv;
      } else if constexpr (type == TargetType::Depth or
                           type == TargetType::Stencil) {
        return cfg.dsv;
      }
    }());
  }
  return VkRenderingAttachmentInfo{
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = image_view,
      .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
      .loadOp = getVkAttachmentLoadOp(load_op),
      .storeOp = getVkAttachmentStoreOp(store_op),
      .clearValue = clear_value,
  };
};
} // namespace

void VulkanCommandBuffer::beginRendering(
    int x, int y, unsigned width, unsigned height,
    SmallVector<RenderTargetConfig, 8> render_targets,
    std::optional<DepthStencilTargetConfig> depth_stencil_target) {

  auto color_attachments =
      render_targets | ranges::views::transform([&](const auto &rt_cfg) {
        return getVkRenderingAttachmentInfo<TargetType::Color>(m_device,
                                                               rt_cfg);
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
    depth_attachment = getVkRenderingAttachmentInfo<TargetType::Depth>(
        m_device, *depth_stencil_target);
    stencil_attachment = getVkRenderingAttachmentInfo<TargetType::Stencil>(
        m_device, *depth_stencil_target);
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

namespace {
VkImageBlit getVkImageBlit(const BlitRegion &region) {
  return {
      .srcSubresource =
          {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .mipLevel = region.src_mip_level,
              .baseArrayLayer = region.src_first_array_layer,
              .layerCount = region.array_layer_count,
          },
      .srcOffsets =
          {
              {int(region.src_offsets[0][0]), int(region.src_offsets[0][1]),
               int(region.src_offsets[0][2])},
              {int(region.src_offsets[1][0]), int(region.src_offsets[1][1]),
               int(region.src_offsets[1][2])},
          },
      .dstSubresource =
          {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .mipLevel = region.dst_mip_level,
              .baseArrayLayer = region.dst_first_array_layer,
              .layerCount = region.array_layer_count,
          },
      .dstOffsets =
          {
              {int(region.dst_offsets[0][0]), int(region.dst_offsets[0][1]),
               int(region.dst_offsets[0][2])},
              {int(region.dst_offsets[1][0]), int(region.dst_offsets[1][1]),
               int(region.dst_offsets[1][2])},
          },
  };
}
} // namespace

void VulkanCommandBuffer::blit(Texture src, Texture dst,
                               std::span<const BlitRegion> regions,
                               Filter filter) {
  auto vk_src = getVkImage(src);
  auto vk_dst = getVkImage(dst);
  auto vk_regions = regions | ranges::views::transform(getVkImageBlit) |
                    ranges::to<SmallVector<VkImageBlit, 8>>;
  m_parent->addFrameResource(std::move(src));
  m_parent->addFrameResource(std::move(dst));
  m_device->CmdBlitImage(
      m_cmd_buffer, vk_src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk_dst,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, vk_regions.size(),
      vk_regions.data(), getVkFilter(filter));
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
