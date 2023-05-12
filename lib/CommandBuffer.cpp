#include "CommandBuffer.hpp"
#include "Device.hpp"
#include "Errors.hpp"
#include "Support/Views.hpp"

namespace ren {

CommandBuffer::CommandBuffer(Device *device, VkCommandBuffer cmd_buffer)
    : m_device(device), m_cmd_buffer(cmd_buffer) {
  VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
}

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
    std::span<const Optional<ColorAttachment>> render_targets,
    Optional<const DepthStencilAttachment &> depth_stencil_target) {
  auto color_attachments =
      render_targets |
      map([&](const Optional<ColorAttachment> &color_attachment) {
        return color_attachment.map_or(
            [&](const ColorAttachment &color_attachment) {
              VkRenderingAttachmentInfo info = {
                  .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                  .imageView =
                      m_device->getVkImageView(color_attachment.texture),
                  .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                  .loadOp = color_attachment.load_op,
                  .storeOp = color_attachment.store_op,
              };
              static_assert(sizeof(info.clearValue.color.float32) ==
                            sizeof(color_attachment.clear_color));
              std::memcpy(info.clearValue.color.float32,
                          &color_attachment.clear_color,
                          sizeof(color_attachment.clear_color));
              return info;
            },
            VkRenderingAttachmentInfo{
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            });
      }) |
      ranges::to<SmallVector<VkRenderingAttachmentInfo, 8>>;

  VkRenderingAttachmentInfo depth_attachment = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};

  VkRenderingAttachmentInfo stencil_attachment = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};

  depth_stencil_target.map([&](const DepthStencilAttachment &dst) {
    VkImageView view = nullptr;

    dst.depth.map([&](const DepthStencilAttachment::Depth &depth) {
      view = m_device->getVkImageView(dst.texture);
      depth_attachment = {
          .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
          .imageView = view,
          .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
          .loadOp = depth.load_op,
          .storeOp = depth.store_op,
          .clearValue = {.depthStencil = {.depth = depth.clear_depth}},
      };
    });

    dst.stencil.map([&](const DepthStencilAttachment::Stencil &stencil) {
      if (!view) {
        view = m_device->getVkImageView(dst.texture);
      }
      stencil_attachment = {
          .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
          .imageView = view,
          .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
          .loadOp = stencil.load_op,
          .storeOp = stencil.store_op,
          .clearValue = {.depthStencil = {.stencil = stencil.clear_stencil}},
      };
    });
  });

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

void CommandBuffer::begin_rendering(TextureView color_target) {
  color_target.num_mip_levels = 1;
  color_target.num_array_layers = 1;

  ColorAttachment color_attachment = {.texture = color_target};
  std::array color_attachments = {Optional<ColorAttachment>(color_attachment)};

  begin_rendering(0, 0, color_target->size.x, color_target->size.y,
                  color_attachments);
}

void CommandBuffer::begin_rendering(TextureView color_target,
                                    TextureView depth_target) {
  assert(depth_target->size.x >= color_target->size.x);
  assert(depth_target->size.y >= color_target->size.y);

  color_target.num_mip_levels = 1;
  color_target.num_array_layers = 1;

  depth_target.num_mip_levels = 1;
  depth_target.num_array_layers = 1;

  ColorAttachment color_attachment = {.texture = color_target};
  std::array color_attachments = {Optional<ColorAttachment>(color_attachment)};

  DepthStencilAttachment depth_attachment = {
      .texture = depth_target,
      .depth = DepthStencilAttachment::Depth{},
  };

  begin_rendering(0, 0, color_target->size.x, color_target->size.y,
                  color_attachments, depth_attachment);
}

void CommandBuffer::end_rendering() { m_device->CmdEndRendering(m_cmd_buffer); }

void CommandBuffer::copy_buffer(const Buffer &src, const Buffer &dst,
                                std::span<const VkBufferCopy> regions) {
  m_device->CmdCopyBuffer(m_cmd_buffer, src.handle, dst.handle, regions.size(),
                          regions.data());
}

void CommandBuffer::copy_buffer(const Buffer &src, const Buffer &dst,
                                const VkBufferCopy &region) {
  copy_buffer(src, dst, asSpan(region));
}

void CommandBuffer::copy_buffer(const BufferView &src, const BufferView &dst) {
  assert(src.size <= dst.size);
  copy_buffer(src.buffer, dst.buffer,
              {
                  .srcOffset = src.offset,
                  .dstOffset = dst.offset,
                  .size = src.size,
              });
}

void CommandBuffer::copy_buffer_to_image(
    const Buffer &src, const Texture &dst,
    std::span<const VkBufferImageCopy> regions) {
  m_device->CmdCopyBufferToImage(m_cmd_buffer, src.handle, dst.image,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 regions.size(), regions.data());
}

void CommandBuffer::blit(const Texture &src, const Texture &dst,
                         std::span<const VkImageBlit> regions,
                         VkFilter filter) {
  m_device->CmdBlitImage(m_cmd_buffer, src.image,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst.image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, regions.size(),
                         regions.data(), filter);
}

void CommandBuffer::blit(const Texture &src, const Texture &dst,
                         const VkImageBlit &region, VkFilter filter) {
  blit(src, dst, asSpan(region), filter);
}

void CommandBuffer::set_viewports(std::span<const VkViewport> in_viewports) {
  SmallVector<VkViewport, 8> viewports(in_viewports.begin(),
                                       in_viewports.end());
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

void CommandBuffer::bind_graphics_pipeline(Handle<GraphicsPipeline> pipeline) {
  m_device->CmdBindPipeline(m_cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_device->get_graphics_pipeline(pipeline).handle);
}

void CommandBuffer::bind_descriptor_sets(
    VkPipelineBindPoint bind_point, Handle<PipelineLayout> layout,
    unsigned first_set, std::span<const VkDescriptorSet> sets) {
  m_device->CmdBindDescriptorSets(
      m_cmd_buffer, bind_point, m_device->get_pipeline_layout(layout).handle,
      first_set, sets.size(), sets.data(), 0, nullptr);
}

void CommandBuffer::set_push_constants(Handle<PipelineLayout> layout,
                                       VkShaderStageFlags stages,
                                       std::span<const std::byte> data,
                                       unsigned offset) {
  assert(not(stages & VK_SHADER_STAGE_COMPUTE_BIT));
  m_device->CmdPushConstants(m_cmd_buffer,
                             m_device->get_pipeline_layout(layout).handle,
                             stages, offset, data.size(), data.data());
}

void CommandBuffer::bind_index_buffer(const BufferView &buffer,
                                      VkIndexType type) {
  m_device->CmdBindIndexBuffer(m_cmd_buffer, buffer->handle, buffer.offset,
                               type);
}

void CommandBuffer::draw_indexed(const DrawIndexedInfo &&draw_info) {
  assert(draw_info.num_indices > 0);
  assert(draw_info.num_instances > 0);
  m_device->CmdDrawIndexed(m_cmd_buffer, draw_info.num_indices,
                           draw_info.num_instances, draw_info.first_index,
                           draw_info.vertex_offset, draw_info.first_instance);
}

void CommandBuffer::pipeline_barrier(const VkDependencyInfo &dependency_info) {
  m_device->CmdPipelineBarrier2(m_cmd_buffer, &dependency_info);
}

} // namespace ren
