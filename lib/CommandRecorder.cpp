#include "CommandRecorder.hpp"
#include "Renderer.hpp"
#include "core/Math.hpp"
#include "core/Views.hpp"

#include <utility>

namespace ren {

namespace {

auto getVkImageAspectFlags(TinyImageFormat format) -> VkImageAspectFlags {
  if (TinyImageFormat_IsDepthAndStencil(format)) {
    return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
  } else if (TinyImageFormat_IsDepthOnly(format)) {
    return VK_IMAGE_ASPECT_DEPTH_BIT;
  } else if (TinyImageFormat_IsStencilOnly(format)) {
    return VK_IMAGE_ASPECT_STENCIL_BIT;
  }
  return VK_IMAGE_ASPECT_COLOR_BIT;
}

auto get_format_aspect_mask(TinyImageFormat format) -> rhi::ImageAspectMask {
  if (TinyImageFormat_IsDepthAndStencil(format) or
      TinyImageFormat_IsDepthOnly(format)) {
    return rhi::ImageAspect::Depth;
  }
  return rhi::ImageAspect::Color;
}

auto get_layout_for_attachment_ops(VkAttachmentLoadOp load,
                                   VkAttachmentStoreOp store) -> VkImageLayout {
  if (load == VK_ATTACHMENT_LOAD_OP_LOAD and
      store == VK_ATTACHMENT_STORE_OP_NONE) {
    return VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
  }
  return VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
}

} // namespace

auto CommandRecorder::begin(Renderer &renderer, Handle<CommandPool> cmd_pool)
    -> Result<void, Error> {
  m_renderer = &renderer;
  ren_try(m_cmd, rhi::begin_command_buffer(
                     renderer.get_rhi_device(),
                     renderer.get_command_pool(cmd_pool).handle));
  return {};
}

auto CommandRecorder::end() -> Result<rhi::CommandBuffer, Error> {
  ren_try_to(rhi::end_command_buffer(m_cmd));
  return std::exchange(m_cmd, {});
}

void CommandRecorder::copy_buffer(Handle<Buffer> src, Handle<Buffer> dst,
                                  TempSpan<const VkBufferCopy> regions) {
  vkCmdCopyBuffer(m_cmd.handle, m_renderer->get_buffer(src).handle.handle,
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
      m_cmd.handle, m_renderer->get_buffer(src).handle.handle,
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
  vkCmdFillBuffer(m_cmd.handle,
                  m_renderer->get_buffer(view.buffer).handle.handle,
                  view.offset, view.size_bytes(), value);
};

void CommandRecorder::update_buffer(const BufferView &view,
                                    TempSpan<const std::byte> data) {
  ren_assert(view.size_bytes() >= data.size());
  ren_assert(data.size() % 4 == 0);
  vkCmdUpdateBuffer(m_cmd.handle,
                    m_renderer->get_buffer(view.buffer).handle.handle,
                    view.offset, view.size_bytes(), data.data());
}

void CommandRecorder::copy_texture_to_buffer(
    Handle<Texture> src, Handle<Buffer> dst,
    TempSpan<const VkBufferImageCopy> regions) {
  vkCmdCopyImageToBuffer(m_cmd.handle,
                         m_renderer->get_texture(src).handle.handle,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         m_renderer->get_buffer(dst).handle.handle,
                         regions.size(), regions.data());
}

void CommandRecorder::copy_texture_to_buffer(Handle<Texture> src,
                                             const BufferView &dst, u32 level) {
  const Texture &texture = m_renderer->get_texture(src);
  ren_assert(level < texture.num_mip_levels);
  glm::uvec3 size = glm::max(texture.size, {1, 1, 1});
  copy_texture_to_buffer(
      src, dst.buffer,
      {{
          .bufferOffset = dst.offset,
          .imageSubresource =
              {
                  .aspectMask = getVkImageAspectFlags(texture.format),
                  .mipLevel = level,
                  .layerCount = texture.num_array_layers,
              },
          .imageExtent = {size.x, size.y, size.z},
      }});
}

void CommandRecorder::blit(Handle<Texture> src, Handle<Texture> dst,
                           TempSpan<const VkImageBlit> regions,
                           VkFilter filter) {
  vkCmdBlitImage(m_cmd.handle, m_renderer->get_texture(src).handle.handle,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 m_renderer->get_texture(dst).handle.handle,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, regions.size(),
                 regions.data(), filter);
}

void CommandRecorder::clear_texture(
    Handle<Texture> texture, TempSpan<const VkClearColorValue> clear_colors,
    TempSpan<const VkImageSubresourceRange> clear_ranges) {
  auto count = std::min<usize>(clear_colors.size(), clear_ranges.size());
  vkCmdClearColorImage(m_cmd.handle,
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
  vkCmdClearColorImage(m_cmd.handle, texture.handle.handle,
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
      m_cmd.handle, m_renderer->get_texture(texture).handle.handle,
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
  vkCmdClearDepthStencilImage(m_cmd.handle, texture.handle.handle,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              &clear_depth_stencil, 1, &clear_range);
}

void CommandRecorder::copy_texture(Handle<Texture> hsrc, Handle<Texture> hdst) {
  const Texture &src = m_renderer->get_texture(hsrc);
  const Texture &dst = m_renderer->get_texture(hdst);
  glm::uvec3 size = glm::min(src.size, dst.size);
  size = glm::max(src.size, {1, 1, 1});
  VkImageCopy region = {.srcSubresource =
                            {
                                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                .layerCount = src.num_array_layers,
                            },
                        .dstSubresource =
                            {
                                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                .layerCount = dst.num_array_layers,
                            },
                        .extent = {size.x, size.y, size.z}};
  vkCmdCopyImage(m_cmd.handle, src.handle.handle,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst.handle.handle,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void CommandRecorder::pipeline_barrier(
    TempSpan<const rhi::MemoryBarrier> memory_barriers,
    TempSpan<const TextureBarrier> texture_barriers) {
  SmallVector<rhi::ImageBarrier, 16> image_barriers(texture_barriers.size());
  for (usize i : range(texture_barriers.size())) {
    const TextureBarrier &barrier = texture_barriers[i];
    const Texture &texture = m_renderer->get_texture(barrier.resource.handle);
    image_barriers[i] = {
        .image = texture.handle,
        .range =
            {
                .aspect_mask = get_format_aspect_mask(texture.format),
                .first_mip_level = barrier.resource.first_mip_level,
                .num_mip_levels = barrier.resource.num_mip_levels,
                .num_array_layers = 1,
            },
        .src_stage_mask = barrier.src_stage_mask,
        .src_access_mask = barrier.src_access_mask,
        .src_layout = barrier.src_layout,
        .dst_stage_mask = barrier.dst_stage_mask,
        .dst_access_mask = barrier.dst_access_mask,
        .dst_layout = barrier.dst_layout,
    };
  }
  rhi::cmd_pipeline_barrier(m_cmd, memory_barriers, image_barriers);
}

auto CommandRecorder::render_pass(const RenderPassBeginInfo &&begin_info)
    -> RenderPass {
  return RenderPass(*m_renderer, m_cmd, std::move(begin_info));
}

RenderPass::RenderPass(Renderer &renderer, rhi::CommandBuffer cmd,
                       const RenderPassBeginInfo &&begin_info) {
  m_renderer = &renderer;
  m_cmd = cmd;

  glm::uvec2 max_size = {-1, -1};
  glm::uvec2 size = max_size;

  StaticVector<VkRenderingAttachmentInfo, rhi::MAX_NUM_RENDER_TARGETS>
      render_targets(begin_info.color_attachments.size());
  for (usize i : range(render_targets.size())) {
    const ColorAttachment &rt = begin_info.color_attachments[i];
    if (!rt.rtv.texture) {
      render_targets[i] = {
          .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      };
    }
    render_targets[i] = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = m_renderer->get_rtv(rt.rtv)->handle,
        .imageLayout = get_layout_for_attachment_ops(rt.ops.load, rt.ops.store),
        .loadOp = rt.ops.load,
        .storeOp = rt.ops.store,
    };
    static_assert(sizeof(render_targets[i].clearValue.color.float32) ==
                  sizeof(rt.ops.clear_color));
    std::memcpy(render_targets[i].clearValue.color.float32, &rt.ops.clear_color,
                sizeof(rt.ops.clear_color));
    size = glm::min(size,
                    glm::uvec2(get_texture_size(*m_renderer, rt.rtv.texture)));
  }

  VkRenderingAttachmentInfo depth_stencil_target = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};

  if (begin_info.depth_stencil_attachment.dsv.texture) {
    const DepthStencilAttachment &dst = begin_info.depth_stencil_attachment;
    depth_stencil_target = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = m_renderer->get_rtv(dst.dsv)->handle,
        .imageLayout =
            get_layout_for_attachment_ops(dst.ops.load, dst.ops.store),
        .loadOp = dst.ops.load,
        .storeOp = dst.ops.store,
        .clearValue = {.depthStencil = {.depth = dst.ops.clear_depth}},
    };
    size = glm::min(size,
                    glm::uvec2(get_texture_size(*m_renderer, dst.dsv.texture)));
  }

  ren_assert_msg(size != max_size, "At least one attachment must be provided");

  VkRenderingInfo rendering_info = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = {.extent = {size.x, size.y}},
      .layerCount = 1,
      .colorAttachmentCount = (u32)render_targets.size(),
      .pColorAttachments = render_targets.data(),
      .pDepthAttachment = &depth_stencil_target,
      .pStencilAttachment = nullptr,
  };

  vkCmdBeginRendering(m_cmd.handle, &rendering_info);
}

RenderPass::~RenderPass() {
  if (m_cmd) {
    end();
  }
}

void RenderPass::end() {
  vkCmdEndRendering(m_cmd.handle);
  m_cmd = {};
}

void RenderPass::set_viewports(
    StaticVector<VkViewport, rhi::MAX_NUM_RENDER_TARGETS> viewports) {
  for (auto &viewport : viewports) {
    viewport.y += viewport.height;
    viewport.height = -viewport.height;
  }
  vkCmdSetViewportWithCount(m_cmd.handle, viewports.size(), viewports.data());
}

void RenderPass::set_scissor_rects(TempSpan<const VkRect2D> rects) {
  vkCmdSetScissorWithCount(m_cmd.handle, rects.size(), rects.data());
}

void RenderPass::set_depth_compare_op(VkCompareOp op) {
  vkCmdSetDepthCompareOp(m_cmd.handle, op);
}

void RenderPass::bind_graphics_pipeline(Handle<GraphicsPipeline> handle) {
  const auto &pipeline = m_renderer->get_graphics_pipeline(handle);
  m_pipeline_layout = pipeline.layout;
  vkCmdBindPipeline(m_cmd.handle, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipeline.handle.handle);
}

void RenderPass::set_descriptor_heaps(
    Handle<ResourceDescriptorHeap> resource_heap,
    Handle<SamplerDescriptorHeap> sampler_heap) {
  rhi::cmd_set_descriptor_heaps(
      {m_cmd.handle, m_renderer->get_rhi_device()},
      rhi::PipelineBindPoint::Graphics,
      m_renderer->get_resource_descriptor_heap(resource_heap).handle,
      m_renderer->get_sampler_descriptor_heap(sampler_heap).handle);
}

void RenderPass::set_push_constants(Handle<PipelineLayout> layout,
                                    TempSpan<const std::byte> data,
                                    unsigned offset) {
  vkCmdPushConstants(m_cmd.handle,
                     m_renderer->get_pipeline_layout(layout).handle.handle,
                     VK_SHADER_STAGE_ALL, offset, data.size(), data.data());
}

void RenderPass::set_push_constants(TempSpan<const std::byte> data,
                                    unsigned offset) {
  ren_assert_msg(m_pipeline_layout, "A graphics pipeline must be bound");
  set_push_constants(m_pipeline_layout, data, offset);
}

void RenderPass::bind_index_buffer(Handle<Buffer> buffer, VkIndexType type,
                                   u32 offset) {
  vkCmdBindIndexBuffer(
      m_cmd.handle, m_renderer->get_buffer(buffer).handle.handle, offset, type);
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
  vkCmdDraw(m_cmd.handle, draw_info.num_vertices, draw_info.num_instances,
            draw_info.first_vertex, draw_info.first_instance);
}

void RenderPass::draw_indexed(const DrawIndexedInfo &&draw_info) {
  vkCmdDrawIndexed(m_cmd.handle, draw_info.num_indices, draw_info.num_instances,
                   draw_info.first_index, draw_info.vertex_offset,
                   draw_info.first_instance);
}

void RenderPass::draw_indirect(const BufferView &view, usize stride) {
  const Buffer &buffer = m_renderer->get_buffer(view.buffer);
  usize count = (view.size_bytes() + stride - sizeof(VkDrawIndirectCommand)) /
                sizeof(VkDrawIndirectCommand);
  vkCmdDrawIndirect(m_cmd.handle, buffer.handle.handle, view.offset, count,
                    stride);
}

void RenderPass::draw_indirect_count(const BufferView &view,
                                     const BufferView &counter, usize stride) {
  const Buffer &buffer = m_renderer->get_buffer(view.buffer);
  const Buffer &count_buffer = m_renderer->get_buffer(counter.buffer);
  usize max_count =
      (view.size_bytes() + stride - sizeof(VkDrawIndirectCommand)) /
      sizeof(VkDrawIndirectCommand);
  vkCmdDrawIndexedIndirectCount(m_cmd.handle, buffer.handle.handle, view.offset,
                                count_buffer.handle.handle, counter.offset,
                                max_count, stride);
}

void RenderPass::draw_indexed_indirect(const BufferView &view, usize stride) {
  const Buffer &buffer = m_renderer->get_buffer(view.buffer);
  usize count =
      (view.size_bytes() + stride - sizeof(VkDrawIndexedIndirectCommand)) /
      sizeof(VkDrawIndexedIndirectCommand);
  vkCmdDrawIndexedIndirect(m_cmd.handle, buffer.handle.handle, view.offset,
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
  vkCmdDrawIndexedIndirectCount(m_cmd.handle, buffer.handle.handle, view.offset,
                                count_buffer.handle.handle, counter.offset,
                                max_count, stride);
}

void RenderPass::draw_indexed_indirect_count(
    const BufferSlice<glsl::DrawIndexedIndirectCommand> &commands,
    const BufferSlice<u32> &counter) {
  ren_assert(counter.count > 0);
  draw_indexed_indirect_count(BufferView(commands), BufferView(counter));
}

void CommandRecorder::bind_compute_pipeline(Handle<ComputePipeline> handle) {
  const auto &pipeline = m_renderer->get_compute_pipeline(handle);
  m_pipeline = handle;
  m_pipeline_layout = pipeline.layout;
  vkCmdBindPipeline(m_cmd.handle, VK_PIPELINE_BIND_POINT_COMPUTE,
                    pipeline.handle.handle);
}

void CommandRecorder::set_descriptor_heaps(
    Handle<ResourceDescriptorHeap> resource_heap,
    Handle<SamplerDescriptorHeap> sampler_heap) {
  rhi::cmd_set_descriptor_heaps(
      {m_cmd.handle, m_renderer->get_rhi_device()},
      rhi::PipelineBindPoint::Compute,
      m_renderer->get_resource_descriptor_heap(resource_heap).handle,
      m_renderer->get_sampler_descriptor_heap(sampler_heap).handle);
}

void CommandRecorder::set_push_constants(Handle<PipelineLayout> layout,
                                         TempSpan<const std::byte> data,
                                         unsigned offset) {
  vkCmdPushConstants(m_cmd.handle,
                     m_renderer->get_pipeline_layout(layout).handle.handle,
                     VK_SHADER_STAGE_ALL, offset, data.size(), data.data());
}

void CommandRecorder::set_push_constants(TempSpan<const std::byte> data,
                                         unsigned offset) {
  ren_assert_msg(m_pipeline_layout, "A compute pipeline must be bound");
  set_push_constants(m_pipeline_layout, data, offset);
}

void CommandRecorder::dispatch(u32 num_groups_x, u32 num_groups_y,
                               u32 num_groups_z) {
  vkCmdDispatch(m_cmd.handle, num_groups_x, num_groups_y, num_groups_z);
}

void CommandRecorder::dispatch(glm::uvec2 num_groups) {
  dispatch(num_groups.x, num_groups.y);
}

void CommandRecorder::dispatch(glm::uvec3 num_groups) {
  dispatch(num_groups.x, num_groups.y, num_groups.z);
}

void CommandRecorder::dispatch_grid(u32 size, u32 group_size_mult) {
  dispatch_grid_3d({size, 1, 1}, {group_size_mult, 1, 1});
}

void CommandRecorder::dispatch_grid_2d(glm::uvec2 size,
                                       glm::uvec2 group_size_mult) {
  dispatch_grid_3d({size, 1}, {group_size_mult, 1});
}

void CommandRecorder::dispatch_grid_3d(glm::uvec3 size,
                                       glm::uvec3 group_size_mult) {
  glm::uvec3 block_size =
      m_renderer->get_compute_pipeline(m_pipeline).local_size * group_size_mult;
  glm::uvec3 num_groups;
  for (int i = 0; i < num_groups.length(); ++i) {
    num_groups[i] = ceil_div(size[i], block_size[i]);
  }
  dispatch(num_groups);
}

void CommandRecorder::dispatch_indirect(const BufferView &view) {
  ren_assert(view.size_bytes() >= sizeof(VkDispatchIndirectCommand));
  vkCmdDispatchIndirect(m_cmd.handle,
                        m_renderer->get_buffer(view.buffer).handle.handle,
                        view.offset);
}

void CommandRecorder::dispatch_indirect(
    const BufferSlice<glsl::DispatchIndirectCommand> &slice) {
  dispatch_indirect(BufferView(slice));
}

auto CommandRecorder::debug_region(const char *label) -> DebugRegion {
  return DebugRegion(m_cmd, label);
}

DebugRegion::DebugRegion(rhi::CommandBuffer cmd, const char *label) {
  m_cmd = cmd;
#if REN_DEBUG_NAMES
  ren_assert(label);
  VkDebugUtilsLabelEXT label_info = {
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
      .pLabelName = label,
  };
  vkCmdBeginDebugUtilsLabelEXT(m_cmd.handle, &label_info);
#endif
}

DebugRegion::~DebugRegion() {
  if (m_cmd) {
    end();
  }
}

void DebugRegion::end() {
#if REN_DEBUG_NAMES
  vkCmdEndDebugUtilsLabelEXT(m_cmd.handle);
#endif
  m_cmd = {};
}

} // namespace ren
