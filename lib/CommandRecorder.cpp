#include "CommandRecorder.hpp"
#include "Formats.hpp"
#include "Renderer.hpp"
#include "core/Errors.hpp"
#include "core/Math.hpp"
#include "core/Views.hpp"

#include <ranges>

namespace ren {

namespace {

auto get_layout_for_attachment_ops(VkAttachmentLoadOp load,
                                   VkAttachmentStoreOp store) -> VkImageLayout {
  if (load == VK_ATTACHMENT_LOAD_OP_LOAD and
      store == VK_ATTACHMENT_STORE_OP_NONE) {
    return VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
  }
  return VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
}

} // namespace

CommandRecorder::CommandRecorder(Renderer &renderer,
                                 VkCommandBuffer cmd_buffer) {
  ren_assert(cmd_buffer);
  m_renderer = &renderer;
  m_cmd_buffer = cmd_buffer;
  VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  throw_if_failed(vkBeginCommandBuffer(m_cmd_buffer, &begin_info),
                  "Vulkan: Failed to begin command buffer");
}

CommandRecorder::~CommandRecorder() {
  throw_if_failed(vkEndCommandBuffer(m_cmd_buffer),
                  "Vulkan: Failed to end command buffer");
}

void CommandRecorder::copy_buffer(Handle<Buffer> src, Handle<Buffer> dst,
                                  TempSpan<const VkBufferCopy> regions) {
  vkCmdCopyBuffer(m_cmd_buffer, m_renderer->get_buffer(src).handle.handle,
                  m_renderer->get_buffer(dst).handle.handle, regions.size(),
                  regions.data());
}

void CommandRecorder::copy_buffer(const BufferView &src,
                                  const BufferView &dst) {
  ren_assert(src.size_bytes() <= dst.size_bytes());
  copy_buffer(src.buffer, dst.buffer,
              {{
                  .srcOffset = src.offset,
                  .dstOffset = dst.offset,
                  .size = src.size_bytes(),
              }});
}

void CommandRecorder::copy_buffer_to_texture(
    Handle<Buffer> src, Handle<Texture> dst,
    TempSpan<const VkBufferImageCopy> regions) {
  vkCmdCopyBufferToImage(
      m_cmd_buffer, m_renderer->get_buffer(src).handle.handle,
      m_renderer->get_texture(dst).handle.handle,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, regions.size(), regions.data());
}

void CommandRecorder::copy_buffer_to_texture(const BufferView &src,
                                             Handle<Texture> dst, u32 level) {
  const Texture &texture = m_renderer->get_texture(dst);
  ren_assert(level < texture.num_mip_levels);
  glm::uvec3 size = glm::max(texture.size, {1, 1, 1});
  copy_buffer_to_texture(
      src.buffer, dst,
      {{
          .bufferOffset = src.offset,
          .imageSubresource =
              {
                  .aspectMask = getVkImageAspectFlags(texture.format),
                  .mipLevel = level,
                  .layerCount = texture.num_array_layers,
              },
          .imageExtent = {size.x, size.y, size.z},
      }});
}

void CommandRecorder::fill_buffer(const BufferView &view, u32 value) {
  ren_assert(view.offset % alignof(u32) == 0);
  ren_assert(view.size_bytes() % sizeof(u32) == 0);
  vkCmdFillBuffer(m_cmd_buffer,
                  m_renderer->get_buffer(view.buffer).handle.handle,
                  view.offset, view.size_bytes(), value);
};

void CommandRecorder::update_buffer(const BufferView &view,
                                    TempSpan<const std::byte> data) {
  ren_assert(view.size_bytes() >= data.size());
  ren_assert(data.size() % 4 == 0);
  vkCmdUpdateBuffer(m_cmd_buffer,
                    m_renderer->get_buffer(view.buffer).handle.handle,
                    view.offset, view.size_bytes(), data.data());
}

void CommandRecorder::blit(Handle<Texture> src, Handle<Texture> dst,
                           TempSpan<const VkImageBlit> regions,
                           VkFilter filter) {
  vkCmdBlitImage(m_cmd_buffer, m_renderer->get_texture(src).handle.handle,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 m_renderer->get_texture(dst).handle.handle,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, regions.size(),
                 regions.data(), filter);
}

void CommandRecorder::clear_texture(
    Handle<Texture> texture, TempSpan<const VkClearColorValue> clear_colors,
    TempSpan<const VkImageSubresourceRange> clear_ranges) {
  auto count = std::min<usize>(clear_colors.size(), clear_ranges.size());
  vkCmdClearColorImage(m_cmd_buffer,
                       m_renderer->get_texture(texture).handle.handle,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       clear_colors.data(), count, clear_ranges.data());
}

void CommandRecorder::clear_texture(Handle<Texture> htexture,
                                    const VkClearColorValue &clear_color) {
  const Texture &texture = m_renderer->get_texture(htexture);
  VkImageSubresourceRange clear_range = {
      .aspectMask = getVkImageAspectFlags(texture.format),
      .levelCount = texture.num_mip_levels,
      .layerCount = texture.num_array_layers,
  };
  vkCmdClearColorImage(m_cmd_buffer, texture.handle.handle,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_color, 1,
                       &clear_range);
}

void CommandRecorder::clear_texture(Handle<Texture> texture,
                                    const glm::vec4 &clear_color) {
  clear_texture(texture,
                VkClearColorValue{.float32 = {clear_color.r, clear_color.g,
                                              clear_color.b, clear_color.a}});
}

void CommandRecorder::clear_texture(
    Handle<Texture> texture,
    TempSpan<const VkClearDepthStencilValue> clear_depth_stencils,
    TempSpan<const VkImageSubresourceRange> clear_ranges) {
  auto count =
      std::min<usize>(clear_depth_stencils.size(), clear_ranges.size());
  vkCmdClearDepthStencilImage(
      m_cmd_buffer, m_renderer->get_texture(texture).handle.handle,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, clear_depth_stencils.data(), count,
      clear_ranges.data());
}

void CommandRecorder::clear_texture(
    Handle<Texture> htexture,
    const VkClearDepthStencilValue &clear_depth_stencil) {
  const Texture &texture = m_renderer->get_texture(htexture);
  VkImageSubresourceRange clear_range = {
      .aspectMask = getVkImageAspectFlags(texture.format),
      .levelCount = texture.num_mip_levels,
      .layerCount = texture.num_array_layers,
  };
  vkCmdClearDepthStencilImage(m_cmd_buffer, texture.handle.handle,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              &clear_depth_stencil, 1, &clear_range);
}

void CommandRecorder::copy_texture(Handle<Texture> hsrc, Handle<Texture> hdst) {
  const Texture &src = m_renderer->get_texture(hsrc);
  const Texture &dst = m_renderer->get_texture(hdst);
  ren_assert(src.size == dst.size);
  VkImageCopy region = {.extent = {src.size.x, src.size.y, src.size.z}};
  vkCmdCopyImage(m_cmd_buffer, src.handle.handle,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst.handle.handle,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void CommandRecorder::pipeline_barrier(
    const VkDependencyInfo &dependency_info) {
  if (!dependency_info.memoryBarrierCount and
      !dependency_info.bufferMemoryBarrierCount and
      !dependency_info.imageMemoryBarrierCount) {
    return;
  }
  vkCmdPipelineBarrier2(m_cmd_buffer, &dependency_info);
}

void CommandRecorder::pipeline_barrier(
    TempSpan<const VkMemoryBarrier2> barriers,
    TempSpan<const VkImageMemoryBarrier2> image_barriers) {
  VkDependencyInfo dependency = {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .memoryBarrierCount = unsigned(barriers.size()),
      .pMemoryBarriers = barriers.data(),
      .imageMemoryBarrierCount = unsigned(image_barriers.size()),
      .pImageMemoryBarriers = image_barriers.data(),
  };
  pipeline_barrier(dependency);
}

auto CommandRecorder::render_pass(const RenderPassBeginInfo &&begin_info)
    -> RenderPass {
  return RenderPass(*m_renderer, m_cmd_buffer, std::move(begin_info));
}

auto CommandRecorder::compute_pass() -> ComputePass {
  return ComputePass(*m_renderer, m_cmd_buffer);
}

auto CommandRecorder::debug_region(const char *label) -> DebugRegion {
  return DebugRegion(m_cmd_buffer, label);
}

RenderPass::RenderPass(Renderer &renderer, VkCommandBuffer cmd_buffer,
                       const RenderPassBeginInfo &&begin_info) {
  m_renderer = &renderer;
  m_cmd_buffer = cmd_buffer;

  glm::uvec2 max_size = {-1, -1};
  glm::uvec2 size = max_size;
  u32 max_layers = -1;
  u32 layers = max_layers;

  auto color_attachments =
      begin_info.color_attachments |
      map([&](const Optional<ColorAttachment> &att) {
        return att.map_or(
            [&](const ColorAttachment &att) {
              VkRenderingAttachmentInfo info = {
                  .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                  .imageView = m_renderer->getVkImageView(att.texture),
                  .imageLayout = get_layout_for_attachment_ops(att.ops.load,
                                                               att.ops.store),
                  .loadOp = att.ops.load,
                  .storeOp = att.ops.store,
              };
              static_assert(sizeof(info.clearValue.color.float32) ==
                            sizeof(att.ops.clear_color));
              std::memcpy(info.clearValue.color.float32, &att.ops.clear_color,
                          sizeof(att.ops.clear_color));
              size = glm::min(
                  size,
                  glm::uvec2(m_renderer->get_texture_view_size(att.texture)));
              layers = glm::min(layers, att.texture.num_array_layers);
              return info;
            },
            VkRenderingAttachmentInfo{
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            });
      }) |
      std::ranges::to<SmallVector<VkRenderingAttachmentInfo, 8>>();

  VkRenderingAttachmentInfo depth_attachment = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};

  VkRenderingAttachmentInfo stencil_attachment = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};

  begin_info.depth_stencil_attachment.map(
      [&](const DepthStencilAttachment &att) {
        VkImageView view = nullptr;

        att.depth_ops.map([&](const DepthAttachmentOperations &ops) {
          view = m_renderer->getVkImageView(att.texture);
          depth_attachment = {
              .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
              .imageView = view,
              .imageLayout = get_layout_for_attachment_ops(ops.load, ops.store),
              .loadOp = ops.load,
              .storeOp = ops.store,
              .clearValue = {.depthStencil = {.depth = ops.clear_depth}},
          };
        });

        att.stencil_ops.map([&](const StencilAttachmentOperations &ops) {
          view = view ? view : m_renderer->getVkImageView(att.texture);
          stencil_attachment = {
              .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
              .imageView = view,
              .imageLayout = get_layout_for_attachment_ops(ops.load, ops.store),
              .loadOp = ops.load,
              .storeOp = ops.store,
              .clearValue = {.depthStencil = {.stencil = ops.clear_stencil}},
          };
        });

        size = glm::min(
            size, glm::uvec2(m_renderer->get_texture_view_size(att.texture)));
        layers = glm::min(layers, att.texture.num_array_layers);
      });

  ren_assert_msg(size != max_size, "At least one attachment must be provided");
  ren_assert_msg(layers != max_layers,
                 "At least one attachment must be provided");

  VkRenderingInfo rendering_info = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = {.extent = {size.x, size.y}},
      .layerCount = layers,
      .colorAttachmentCount = u32(color_attachments.size()),
      .pColorAttachments = color_attachments.data(),
      .pDepthAttachment = &depth_attachment,
      .pStencilAttachment = &stencil_attachment,
  };

  vkCmdBeginRendering(m_cmd_buffer, &rendering_info);
}

RenderPass::~RenderPass() { vkCmdEndRendering(m_cmd_buffer); }

void RenderPass::set_viewports(
    StaticVector<VkViewport, MAX_COLOR_ATTACHMENTS> viewports) {
  for (auto &viewport : viewports) {
    viewport.y += viewport.height;
    viewport.height = -viewport.height;
  }
  vkCmdSetViewportWithCount(m_cmd_buffer, viewports.size(), viewports.data());
}

void RenderPass::set_scissor_rects(TempSpan<const VkRect2D> rects) {
  vkCmdSetScissorWithCount(m_cmd_buffer, rects.size(), rects.data());
}

void RenderPass::set_depth_compare_op(VkCompareOp op) {
  vkCmdSetDepthCompareOp(m_cmd_buffer, op);
}

void RenderPass::bind_graphics_pipeline(Handle<GraphicsPipeline> handle) {
  const auto &pipeline = m_renderer->get_graphics_pipeline(handle);
  m_pipeline_layout = pipeline.layout;
  m_shader_stages = pipeline.stages;
  vkCmdBindPipeline(m_cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipeline.handle);
}

void RenderPass::bind_descriptor_sets(Handle<PipelineLayout> layout,
                                      TempSpan<const VkDescriptorSet> sets,
                                      unsigned first_set) {
  vkCmdBindDescriptorSets(m_cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          m_renderer->get_pipeline_layout(layout).handle,
                          first_set, sets.size(), sets.data(), 0, nullptr);
}

void RenderPass::set_push_constants(Handle<PipelineLayout> layout,
                                    VkShaderStageFlags stages,
                                    TempSpan<const std::byte> data,
                                    unsigned offset) {
  ren_assert_msg((stages & VK_SHADER_STAGE_ALL_GRAPHICS) == stages,
                 "Only graphics shader stages must be used");
  vkCmdPushConstants(m_cmd_buffer,
                     m_renderer->get_pipeline_layout(layout).handle, stages,
                     offset, data.size(), data.data());
}

void RenderPass::bind_descriptor_sets(TempSpan<const VkDescriptorSet> sets,
                                      unsigned first_set) {
  ren_assert_msg(m_pipeline_layout, "A graphics pipeline must be bound");
  bind_descriptor_sets(m_pipeline_layout, sets, first_set);
}

void RenderPass::set_push_constants(TempSpan<const std::byte> data,
                                    unsigned offset) {
  ren_assert_msg(m_pipeline_layout, "A graphics pipeline must be bound");
  set_push_constants(m_pipeline_layout, m_shader_stages, data, offset);
}

void RenderPass::bind_index_buffer(Handle<Buffer> buffer, VkIndexType type,
                                   u32 offset) {
  vkCmdBindIndexBuffer(
      m_cmd_buffer, m_renderer->get_buffer(buffer).handle.handle, offset, type);
}

void RenderPass::bind_index_buffer(const BufferView &view, VkIndexType type) {
  bind_index_buffer(view.buffer, type, view.offset);
}

void RenderPass::bind_index_buffer(const BufferSlice<u8> &slice) {
  bind_index_buffer(slice.buffer, VK_INDEX_TYPE_UINT8_EXT, slice.offset);
}

void RenderPass::bind_index_buffer(const BufferSlice<u16> &slice) {
  bind_index_buffer(slice.buffer, VK_INDEX_TYPE_UINT16, slice.offset);
}

void RenderPass::bind_index_buffer(const BufferSlice<u32> &slice) {
  bind_index_buffer(slice.buffer, VK_INDEX_TYPE_UINT32, slice.offset);
}

void RenderPass::draw(const DrawInfo &&draw_info) {
  vkCmdDraw(m_cmd_buffer, draw_info.num_vertices, draw_info.num_instances,
            draw_info.first_vertex, draw_info.first_instance);
}

void RenderPass::draw_indexed(const DrawIndexedInfo &&draw_info) {
  vkCmdDrawIndexed(m_cmd_buffer, draw_info.num_indices, draw_info.num_instances,
                   draw_info.first_index, draw_info.vertex_offset,
                   draw_info.first_instance);
}

void RenderPass::draw_indirect(const BufferView &view, usize stride) {
  const Buffer &buffer = m_renderer->get_buffer(view.buffer);
  usize count = (view.size_bytes() + stride - sizeof(VkDrawIndirectCommand)) /
                sizeof(VkDrawIndirectCommand);
  vkCmdDrawIndirect(m_cmd_buffer, buffer.handle.handle, view.offset, count,
                    stride);
}

void RenderPass::draw_indirect_count(const BufferView &view,
                                     const BufferView &counter, usize stride) {
  const Buffer &buffer = m_renderer->get_buffer(view.buffer);
  const Buffer &count_buffer = m_renderer->get_buffer(counter.buffer);
  usize max_count =
      (view.size_bytes() + stride - sizeof(VkDrawIndirectCommand)) /
      sizeof(VkDrawIndirectCommand);
  vkCmdDrawIndexedIndirectCount(m_cmd_buffer, buffer.handle.handle, view.offset,
                                count_buffer.handle.handle, counter.offset,
                                max_count, stride);
}

void RenderPass::draw_indexed_indirect(const BufferView &view, usize stride) {
  const Buffer &buffer = m_renderer->get_buffer(view.buffer);
  usize count =
      (view.size_bytes() + stride - sizeof(VkDrawIndexedIndirectCommand)) /
      sizeof(VkDrawIndexedIndirectCommand);
  vkCmdDrawIndexedIndirect(m_cmd_buffer, buffer.handle.handle, view.offset,
                           count, stride);
}

void RenderPass::draw_indexed_indirect_count(const BufferView &view,
                                             const BufferView &counter,
                                             usize stride) {
  const Buffer &buffer = m_renderer->get_buffer(view.buffer);
  const Buffer &count_buffer = m_renderer->get_buffer(counter.buffer);
  usize max_count =
      (view.size_bytes() + stride - sizeof(VkDrawIndexedIndirectCommand)) /
      sizeof(VkDrawIndexedIndirectCommand);
  vkCmdDrawIndexedIndirectCount(m_cmd_buffer, buffer.handle.handle, view.offset,
                                count_buffer.handle.handle, counter.offset,
                                max_count, stride);
}

void RenderPass::draw_indexed_indirect_count(
    const BufferSlice<glsl::DrawIndexedIndirectCommand> &commands,
    const BufferSlice<u32> &counter) {
  ren_assert(counter.count > 0);
  draw_indexed_indirect_count(BufferView(commands), BufferView(counter));
}

ComputePass::ComputePass(Renderer &renderer, VkCommandBuffer cmd_buffer) {
  m_renderer = &renderer;
  m_cmd_buffer = cmd_buffer;
}

ComputePass::~ComputePass() {}

void ComputePass::bind_compute_pipeline(Handle<ComputePipeline> handle) {
  const auto &pipeline = m_renderer->get_compute_pipeline(handle);
  m_pipeline = handle;
  m_pipeline_layout = pipeline.layout;
  vkCmdBindPipeline(m_cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                    pipeline.handle);
}

void ComputePass::bind_descriptor_sets(Handle<PipelineLayout> layout,
                                       TempSpan<const VkDescriptorSet> sets,
                                       unsigned first_set) {
  vkCmdBindDescriptorSets(m_cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                          m_renderer->get_pipeline_layout(layout).handle,
                          first_set, sets.size(), sets.data(), 0, nullptr);
}

void ComputePass::bind_descriptor_sets(TempSpan<const VkDescriptorSet> sets,
                                       unsigned first_set) {
  ren_assert_msg(m_pipeline_layout, "A compute pipeline must be bound");
  bind_descriptor_sets(m_pipeline_layout, sets, first_set);
}

void ComputePass::set_push_constants(Handle<PipelineLayout> layout,
                                     TempSpan<const std::byte> data,
                                     unsigned offset) {
  vkCmdPushConstants(
      m_cmd_buffer, m_renderer->get_pipeline_layout(layout).handle,
      VK_SHADER_STAGE_COMPUTE_BIT, offset, data.size(), data.data());
}

void ComputePass::set_push_constants(TempSpan<const std::byte> data,
                                     unsigned offset) {
  ren_assert_msg(m_pipeline_layout, "A compute pipeline must be bound");
  set_push_constants(m_pipeline_layout, data, offset);
}

void ComputePass::dispatch(u32 num_groups_x, u32 num_groups_y,
                           u32 num_groups_z) {
  vkCmdDispatch(m_cmd_buffer, num_groups_x, num_groups_y, num_groups_z);
}

void ComputePass::dispatch(glm::uvec2 num_groups) {
  dispatch(num_groups.x, num_groups.y);
}

void ComputePass::dispatch(glm::uvec3 num_groups) {
  dispatch(num_groups.x, num_groups.y, num_groups.z);
}

void ComputePass::dispatch_grid(u32 size, u32 group_size_mult) {
  dispatch_grid_3d({size, 1, 1}, {group_size_mult, 1, 1});
}

void ComputePass::dispatch_grid_2d(glm::uvec2 size,
                                   glm::uvec2 group_size_mult) {
  dispatch_grid_3d({size, 1}, {group_size_mult, 1});
}

void ComputePass::dispatch_grid_3d(glm::uvec3 size,
                                   glm::uvec3 group_size_mult) {
  glm::uvec3 block_size =
      m_renderer->get_compute_pipeline(m_pipeline).local_size * group_size_mult;
  glm::uvec3 num_groups;
  for (int i = 0; i < num_groups.length(); ++i) {
    num_groups[i] = ceil_div(size[i], block_size[i]);
  }
  dispatch(num_groups);
}

void ComputePass::dispatch_indirect(const BufferView &view) {
  ren_assert(view.size_bytes() >= sizeof(VkDispatchIndirectCommand));
  vkCmdDispatchIndirect(m_cmd_buffer,
                        m_renderer->get_buffer(view.buffer).handle.handle,
                        view.offset);
}

void ComputePass::dispatch_indirect(
    const BufferSlice<glsl::DispatchIndirectCommand> &slice) {
  dispatch_indirect(BufferView(slice));
}

DebugRegion::DebugRegion(VkCommandBuffer cmd_buffer, const char *label) {
  m_cmd_buffer = cmd_buffer;
#if REN_DEBUG_NAMES
  ren_assert(label);
  VkDebugUtilsLabelEXT label_info = {
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
      .pLabelName = label,
  };
  vkCmdBeginDebugUtilsLabelEXT(m_cmd_buffer, &label_info);
#endif
}

DebugRegion::DebugRegion(DebugRegion &&other) {
  m_cmd_buffer = std::exchange(other.m_cmd_buffer, nullptr);
}

DebugRegion::~DebugRegion() {
#if REN_DEBUG_NAMES
  if (m_cmd_buffer) {
    vkCmdEndDebugUtilsLabelEXT(m_cmd_buffer);
  }
#endif
}

} // namespace ren
