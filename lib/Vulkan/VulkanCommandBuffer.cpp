#include "Vulkan/VulkanCommandBuffer.hpp"
#include "Support/Enum.hpp"
#include "Vulkan/VulkanCommandAllocator.hpp"
#include "Vulkan/VulkanDevice.hpp"
#include "Vulkan/VulkanPipelineStages.hpp"
#include "Vulkan/VulkanSync.inl"
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
constexpr auto render_target_load_op_map = std::array{
    std::pair(RenderTargetLoadOp::Clear, VK_ATTACHMENT_LOAD_OP_CLEAR),
    std::pair(RenderTargetLoadOp::Load, VK_ATTACHMENT_LOAD_OP_LOAD),
    std::pair(RenderTargetLoadOp::Discard, VK_ATTACHMENT_LOAD_OP_DONT_CARE),
};

constexpr auto render_target_store_op_map = std::array{
    std::pair(RenderTargetStoreOp::Store, VK_ATTACHMENT_STORE_OP_STORE),
    std::pair(RenderTargetStoreOp::Discard, VK_ATTACHMENT_STORE_OP_DONT_CARE),
};

constexpr auto getVkAttachmentLoadOp = enumMap<render_target_load_op_map>;
constexpr auto getVkAttachmentStoreOp = enumMap<render_target_store_op_map>;

template <typename T>
VkRenderingAttachmentInfo getVkRenderingAttachmentInfo(VulkanDevice *device,
                                                       const T &rt_cfg) {
  return VkRenderingAttachmentInfo{
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = device->getVkImageView(rt_cfg.view),
      .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
      .loadOp = getVkAttachmentLoadOp(rt_cfg.load_op),
      .storeOp = getVkAttachmentStoreOp(rt_cfg.store_op),
      .clearValue = [&]() -> VkClearValue {
        if constexpr (std::same_as<T, RenderTargetConfig>) {
          const auto &cc = rt_cfg.clear_color;
          return {.color = {.float32 = {cc[0], cc[1], cc[2], cc[3]}}};
        } else if constexpr (std::same_as<T, DepthRenderTargetConfig>) {
          return {.depthStencil = {.depth = rt_cfg.clear_depth}};
        } else if constexpr (std::same_as<T, StencilRenderTargetConfig>) {
          return {.depthStencil = {.stencil = rt_cfg.clear_stencil}};
        }
      }(),
  };
};
} // namespace

void VulkanCommandBuffer::beginRendering(
    int x, int y, unsigned width, unsigned height,
    SmallVector<RenderTargetConfig, 8> render_targets,
    std::optional<DepthRenderTargetConfig> depth_render_target,
    std::optional<StencilRenderTargetConfig> stencil_render_target) {
  VkRenderingInfo rendering_info = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = {.offset = {x, y}, .extent = {width, height}},
      .layerCount = 1,
  };

  auto color_attachments =
      render_targets | ranges::views::transform([&](const auto &rt_cfg) {
        return getVkRenderingAttachmentInfo(m_device, rt_cfg);
      }) |
      ranges::to<SmallVector<VkRenderingAttachmentInfo, 8>>;
  rendering_info.colorAttachmentCount = color_attachments.size();
  rendering_info.pColorAttachments = color_attachments.data();

  for (auto &rt : render_targets) {
    m_parent->addFrameResource(std::move(rt.view));
  }

  VkRenderingAttachmentInfo depth_attachment, stencil_attachment;
  if (depth_render_target) {
    depth_attachment =
        getVkRenderingAttachmentInfo(m_device, *depth_render_target);
    rendering_info.pDepthAttachment = &depth_attachment;
    m_parent->addFrameResource(std::move(depth_render_target->view));
  }
  if (stencil_render_target) {
    stencil_attachment =
        getVkRenderingAttachmentInfo(m_device, *stencil_render_target);
    rendering_info.pStencilAttachment = &stencil_attachment;
    m_parent->addFrameResource(std::move(stencil_render_target->view));
  }

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

constexpr auto filter_map = std::array{
    std::pair(Filter::Nearest, VK_FILTER_NEAREST),
    std::pair(Filter::Linear, VK_FILTER_LINEAR),
};

constexpr auto getVkFilter = enumMap<filter_map>;
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
