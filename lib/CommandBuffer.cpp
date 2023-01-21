#include "CommandBuffer.hpp"
#include "Support/Views.hpp"
#include "Vulkan/VulkanDevice.hpp"

namespace ren {

CommandBuffer::CommandBuffer(VulkanDevice *device, VkCommandBuffer cmd_buffer)
    : m_device(device), m_cmd_buffer(cmd_buffer) {
  VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
}

const Device &CommandBuffer::get_device() const { return *m_device; }

Device &CommandBuffer::get_device() { return *m_device; }

void CommandBuffer::begin() {
  VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  throwIfFailed(m_device->BeginCommandBuffer(m_cmd_buffer, &begin_info),
                "Vulkan: Failed to begin command buffer");
}

void CommandBuffer::end() {
  throwIfFailed(m_device->EndCommandBuffer(m_cmd_buffer),
                "Vulkan: Failed to end command buffer");
}

void CommandBuffer::begin_rendering(
    int x, int y, unsigned width, unsigned height,
    std::span<const RenderTargetConfig> render_targets,
    const Optional<DepthStencilTargetConfig> &depth_stencil_target) {
  auto attachment_is_used = [](VkAttachmentLoadOp load_op,
                               VkAttachmentStoreOp store_op) {
    return not(load_op == VK_ATTACHMENT_LOAD_OP_DONT_CARE and
               (store_op == VK_ATTACHMENT_STORE_OP_DONT_CARE or
                store_op == VK_ATTACHMENT_STORE_OP_NONE));
  };

  auto color_attachments =
      render_targets |
      ranges::views::transform([&](const RenderTargetConfig &rt) {
        const auto &cc = rt.clear_color;
        return VkRenderingAttachmentInfo{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = attachment_is_used(rt.load_op, rt.store_op)
                             ? m_device->getVkImageView(rt.rtv)
                             : VK_NULL_HANDLE,
            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .loadOp = rt.load_op,
            .storeOp = rt.store_op,
            .clearValue = {.color = {.float32 = {cc[0], cc[1], cc[2], cc[3]}}},
        };
      }) |
      ranges::to<SmallVector<VkRenderingAttachmentInfo, 8>>;

  VkRenderingAttachmentInfo depth_attachment = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  VkRenderingAttachmentInfo stencil_attachment = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};

  if (depth_stencil_target) {
    auto &dst = *depth_stencil_target;
    depth_attachment = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp = dst.depth_load_op,
        .storeOp = dst.depth_store_op,
        .clearValue = {.depthStencil = {.depth = dst.clear_depth}},
    };
    VkImageView view = VK_NULL_HANDLE;
    if (attachment_is_used(dst.depth_load_op, dst.depth_store_op)) {
      view = m_device->getVkImageView(dst.dsv);
      depth_attachment.imageView = view;
    }
    stencil_attachment = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp = dst.stencil_load_op,
        .storeOp = dst.stencil_store_op,
        .clearValue = {.depthStencil = {.stencil = dst.clear_stencil}},
    };
    if (attachment_is_used(dst.stencil_load_op, dst.stencil_store_op)) {
      if (!view) {
        view = m_device->getVkImageView(dst.dsv);
      }
      stencil_attachment.imageView = view;
    }
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

void CommandBuffer::end_rendering() { m_device->CmdEndRendering(m_cmd_buffer); }

void CommandBuffer::copy_buffer(const BufferRef &src, const BufferRef &dst,
                                std::span<const VkBufferCopy> regions) {
  m_device->CmdCopyBuffer(m_cmd_buffer, src.handle, dst.handle, regions.size(),
                          regions.data());
}

void CommandBuffer::blit(const TextureRef &src, const TextureRef &dst,
                         std::span<const VkImageBlit> regions,
                         VkFilter filter) {
  m_device->CmdBlitImage(m_cmd_buffer, src.handle,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst.handle,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, regions.size(),
                         regions.data(), filter);
}

void CommandBuffer::set_viewports(SmallVector<VkViewport, 8> viewports) {
  for (auto &viewport : viewports) {
    viewport.y += viewport.height;
    viewport.height = -viewport.height;
  }
  m_device->CmdSetViewportWithCount(m_cmd_buffer, viewports.size(),
                                    viewports.data());
}

void CommandBuffer::set_scissor_rects(std::span<const VkRect2D> rects) {
  m_device->CmdSetScissorWithCount(m_cmd_buffer, rects.size(), rects.data());
}

void CommandBuffer::bind_graphics_pipeline(GraphicsPipelineRef pipeline) {
  m_device->CmdBindPipeline(m_cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline.handle);
}

void CommandBuffer::bind_graphics_descriptor_sets(
    PipelineLayoutRef layout, unsigned first_set,
    std::span<const VkDescriptorSet> sets) {
  m_device->CmdBindDescriptorSets(m_cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  layout.handle, first_set, sets.size(),
                                  sets.data(), 0, nullptr);
}

void CommandBuffer::set_push_constants(PipelineLayoutRef layout,
                                       VkShaderStageFlags stages,
                                       std::span<const std::byte> data,
                                       unsigned offset) {
  assert(not(stages & VK_SHADER_STAGE_COMPUTE_BIT));
  m_device->CmdPushConstants(m_cmd_buffer, layout.handle, stages, offset,
                             data.size(), data.data());
}

void CommandBuffer::bind_index_buffer(const BufferRef &buffer,
                                      VkIndexType format) {
  m_device->CmdBindIndexBuffer(m_cmd_buffer, buffer.handle, buffer.desc.offset,
                               format);
}

void CommandBuffer::draw_indexed(unsigned num_indices, unsigned num_instances,
                                 unsigned first_index, int vertex_offset,
                                 unsigned first_instance) {
  m_device->CmdDrawIndexed(m_cmd_buffer, num_indices, num_instances,
                           first_index, vertex_offset, first_instance);
}

} // namespace ren
