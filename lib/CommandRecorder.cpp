#include "CommandRecorder.hpp"
#include "Device.hpp"
#include "Support/Errors.hpp"
#include "Support/Views.hpp"

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

auto get_num_dispatch_groups(u32 size, u32 group_size) -> u32 {
  auto num_groups = size / group_size + ((size % group_size) != 0);
  return num_groups;
}

auto get_num_dispatch_groups(glm::uvec2 size, glm::uvec2 group_size)
    -> glm::uvec2 {
  auto num_groups = size / group_size +
                    glm::uvec2(glm::notEqual(size % group_size, glm::uvec2(0)));
  return num_groups;
}

auto get_num_dispatch_groups(glm::uvec3 size, glm::uvec3 group_size)
    -> glm::uvec3 {
  auto num_groups = size / group_size +
                    glm::uvec3(glm::notEqual(size % group_size, glm::uvec3(0)));
  return num_groups;
}

CommandRecorder::CommandRecorder(Device &device, VkCommandBuffer cmd_buffer) {
  assert(cmd_buffer);
  m_device = &device;
  m_cmd_buffer = cmd_buffer;
  VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  throw_if_failed(m_device->BeginCommandBuffer(m_cmd_buffer, &begin_info),
                  "Vulkan: Failed to begin command buffer");
}

CommandRecorder::~CommandRecorder() {
  throw_if_failed(m_device->EndCommandBuffer(m_cmd_buffer),
                  "Vulkan: Failed to end command buffer");
}

void CommandRecorder::copy_buffer(Handle<Buffer> src, Handle<Buffer> dst,
                                  TempSpan<const VkBufferCopy> regions) {
  m_device->CmdCopyBuffer(m_cmd_buffer, m_device->get_buffer(src).handle,
                          m_device->get_buffer(dst).handle, regions.size(),
                          regions.data());
}

void CommandRecorder::copy_buffer(const BufferView &src,
                                  const BufferView &dst) {
  assert(src.size <= dst.size);
  copy_buffer(src.buffer, dst.buffer,
              {{
                  .srcOffset = src.offset,
                  .dstOffset = dst.offset,
                  .size = src.size,
              }});
}

void CommandRecorder::copy_buffer_to_image(
    Handle<Buffer> src, Handle<Texture> dst,
    TempSpan<const VkBufferImageCopy> regions) {
  m_device->CmdCopyBufferToImage(m_cmd_buffer, m_device->get_buffer(src).handle,
                                 m_device->get_texture(dst).image,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 regions.size(), regions.data());
}

void CommandRecorder::fill_buffer(const BufferView &view, u32 value) {
  assert(view.offset % sizeof(u32) == 0);
  assert(view.size % sizeof(u32) == 0);
  m_device->CmdFillBuffer(m_cmd_buffer,
                          m_device->get_buffer(view.buffer).handle, view.offset,
                          view.size, value);
};

template <>
void CommandRecorder::update_buffer<std::byte>(const BufferView &view,
                                               TempSpan<const std::byte> data) {
  assert(view.size >= data.size());
  assert(data.size() % 4 == 0);
  m_device->CmdUpdateBuffer(m_cmd_buffer,
                            m_device->get_buffer(view.buffer).handle,
                            view.offset, view.size, data.data());
}

void CommandRecorder::blit(Handle<Texture> src, Handle<Texture> dst,
                           TempSpan<const VkImageBlit> regions,
                           VkFilter filter) {
  m_device->CmdBlitImage(m_cmd_buffer, m_device->get_texture(src).image,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         m_device->get_texture(dst).image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, regions.size(),
                         regions.data(), filter);
}

void CommandRecorder::pipeline_barrier(
    const VkDependencyInfo &dependency_info) {
  if (!dependency_info.memoryBarrierCount and
      !dependency_info.bufferMemoryBarrierCount and
      !dependency_info.imageMemoryBarrierCount) {
    return;
  }
  m_device->CmdPipelineBarrier2(m_cmd_buffer, &dependency_info);
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
  return RenderPass(*m_device, m_cmd_buffer, std::move(begin_info));
}

auto CommandRecorder::compute_pass() -> ComputePass {
  return ComputePass(*m_device, m_cmd_buffer);
}

auto CommandRecorder::debug_region(const char *label) -> DebugRegion {
  return DebugRegion(*m_device, m_cmd_buffer, label);
}

RenderPass::RenderPass(Device &device, VkCommandBuffer cmd_buffer,
                       const RenderPassBeginInfo &&begin_info) {
  m_device = &device;
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
                  .imageView = m_device->getVkImageView(att.texture),
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
                  size, glm::uvec2(device.get_texture_view_size(att.texture)));
              layers = glm::min(layers, att.texture.num_array_layers);
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

  begin_info.depth_stencil_attachment.map(
      [&](const DepthStencilAttachment &att) {
        VkImageView view = nullptr;

        att.depth_ops.map([&](const DepthAttachmentOperations &ops) {
          view = m_device->getVkImageView(att.texture);
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
          view = view ? view : m_device->getVkImageView(att.texture);
          stencil_attachment = {
              .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
              .imageView = view,
              .imageLayout = get_layout_for_attachment_ops(ops.load, ops.store),
              .loadOp = ops.load,
              .storeOp = ops.store,
              .clearValue = {.depthStencil = {.stencil = ops.clear_stencil}},
          };
        });

        size = glm::min(size,
                        glm::uvec2(device.get_texture_view_size(att.texture)));
        layers = glm::min(layers, att.texture.num_array_layers);
      });

  ren_assert(size != max_size, "At least one attachment must be provided");
  ren_assert(layers != max_layers, "At least one attachment must be provided");

  VkRenderingInfo rendering_info = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = {.extent = {size.x, size.y}},
      .layerCount = layers,
      .colorAttachmentCount = u32(color_attachments.size()),
      .pColorAttachments = color_attachments.data(),
      .pDepthAttachment = &depth_attachment,
      .pStencilAttachment = &stencil_attachment,
  };

  m_device->CmdBeginRendering(m_cmd_buffer, &rendering_info);
}

RenderPass::~RenderPass() { m_device->CmdEndRendering(m_cmd_buffer); }

void RenderPass::set_viewports(
    StaticVector<VkViewport, MAX_COLOR_ATTACHMENTS> viewports) {
  for (auto &viewport : viewports) {
    viewport.y += viewport.height;
    viewport.height = -viewport.height;
  }
  m_device->CmdSetViewportWithCount(m_cmd_buffer, viewports.size(),
                                    viewports.data());
}

void RenderPass::set_scissor_rects(TempSpan<const VkRect2D> rects) {
  m_device->CmdSetScissorWithCount(m_cmd_buffer, rects.size(), rects.data());
}

void RenderPass::bind_graphics_pipeline(Handle<GraphicsPipeline> handle) {
  const auto &pipeline = m_device->get_graphics_pipeline(handle);
  m_pipeline_layout = pipeline.layout;
  m_shader_stages = pipeline.stages;
  m_device->CmdBindPipeline(m_cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline.handle);
}

void RenderPass::bind_descriptor_sets(Handle<PipelineLayout> layout,
                                      TempSpan<const VkDescriptorSet> sets,
                                      unsigned first_set) {
  m_device->CmdBindDescriptorSets(m_cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  m_device->get_pipeline_layout(layout).handle,
                                  first_set, sets.size(), sets.data(), 0,
                                  nullptr);
}

void RenderPass::set_push_constants(Handle<PipelineLayout> layout,
                                    VkShaderStageFlags stages,
                                    TempSpan<const std::byte> data,
                                    unsigned offset) {
  ren_assert((stages & VK_SHADER_STAGE_ALL_GRAPHICS) == stages,
             "Only graphics shader stages must be used");
  m_device->CmdPushConstants(m_cmd_buffer,
                             m_device->get_pipeline_layout(layout).handle,
                             stages, offset, data.size(), data.data());
}

void RenderPass::bind_descriptor_sets(TempSpan<const VkDescriptorSet> sets,
                                      unsigned first_set) {
  ren_assert(m_pipeline_layout, "A graphics pipeline must be bound");
  bind_descriptor_sets(m_pipeline_layout, sets, first_set);
}

void RenderPass::set_push_constants(TempSpan<const std::byte> data,
                                    unsigned offset) {
  ren_assert(m_pipeline_layout, "A graphics pipeline must be bound");
  set_push_constants(m_pipeline_layout, m_shader_stages, data, offset);
}

void RenderPass::bind_index_buffer(const BufferView &view, VkIndexType type) {
  m_device->CmdBindIndexBuffer(m_cmd_buffer,
                               m_device->get_buffer(view.buffer).handle,
                               view.offset, type);
}

void RenderPass::draw_indexed(const DrawIndexedInfo &&draw_info) {
  assert(draw_info.num_indices > 0);
  assert(draw_info.num_instances > 0);
  m_device->CmdDrawIndexed(m_cmd_buffer, draw_info.num_indices,
                           draw_info.num_instances, draw_info.first_index,
                           draw_info.vertex_offset, draw_info.first_instance);
}

ComputePass::ComputePass(Device &device, VkCommandBuffer cmd_buffer) {
  m_device = &device;
  m_cmd_buffer = cmd_buffer;
}

ComputePass::~ComputePass() {}

void ComputePass::bind_compute_pipeline(Handle<ComputePipeline> handle) {
  const auto &pipeline = m_device->get_compute_pipeline(handle);
  m_pipeline_layout = pipeline.layout;
  m_device->CmdBindPipeline(m_cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipeline.handle);
}

void ComputePass::bind_descriptor_sets(Handle<PipelineLayout> layout,
                                       TempSpan<const VkDescriptorSet> sets,
                                       unsigned first_set) {
  m_device->CmdBindDescriptorSets(m_cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                  m_device->get_pipeline_layout(layout).handle,
                                  first_set, sets.size(), sets.data(), 0,
                                  nullptr);
}

void ComputePass::bind_descriptor_sets(TempSpan<const VkDescriptorSet> sets,
                                       unsigned first_set) {
  ren_assert(m_pipeline_layout, "A compute pipeline must be bound");
  bind_descriptor_sets(m_pipeline_layout, sets, first_set);
}

void ComputePass::set_push_constants(Handle<PipelineLayout> layout,
                                     TempSpan<const std::byte> data,
                                     unsigned offset) {
  m_device->CmdPushConstants(
      m_cmd_buffer, m_device->get_pipeline_layout(layout).handle,
      VK_SHADER_STAGE_COMPUTE_BIT, offset, data.size(), data.data());
}

void ComputePass::set_push_constants(TempSpan<const std::byte> data,
                                     unsigned offset) {
  ren_assert(m_pipeline_layout, "A compute pipeline must be bound");
  set_push_constants(m_pipeline_layout, data, offset);
}

void ComputePass::dispatch_groups(u32 num_groups_x, u32 num_groups_y,
                                  u32 num_groups_z) {
  m_device->CmdDispatch(m_cmd_buffer, num_groups_x, num_groups_y, num_groups_z);
}

void ComputePass::dispatch_groups(glm::uvec2 num_groups) {
  dispatch_groups(num_groups.x, num_groups.y);
}

void ComputePass::dispatch_groups(glm::uvec3 num_groups) {
  dispatch_groups(num_groups.x, num_groups.y, num_groups.z);
}

void ComputePass::dispatch_threads(u32 size, u32 group_size) {
  dispatch_groups(get_num_dispatch_groups(size, group_size));
}

void ComputePass::dispatch_threads(glm::uvec2 size, glm::uvec2 group_size) {
  dispatch_groups(get_num_dispatch_groups(size, group_size));
}

void ComputePass::dispatch_threads(glm::uvec3 size, glm::uvec3 group_size) {
  dispatch_groups(get_num_dispatch_groups(size, group_size));
}

DebugRegion::DebugRegion(Device &device, VkCommandBuffer cmd_buffer,
                         const char *label) {
  m_device = &device;
  m_cmd_buffer = cmd_buffer;
#if REN_DEBUG_NAMES
  assert(label);
  VkDebugUtilsLabelEXT label_info = {
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
      .pLabelName = label,
  };
  m_device->CmdBeginDebugUtilsLabelEXT(m_cmd_buffer, &label_info);
#endif
}

DebugRegion::DebugRegion(DebugRegion &&other) {
  m_device = std::exchange(other.m_device, nullptr);
  m_cmd_buffer = std::exchange(other.m_cmd_buffer, nullptr);
}

DebugRegion::~DebugRegion() {
#if REN_DEBUG_NAMES
  if (m_cmd_buffer) {
    m_device->CmdEndDebugUtilsLabelEXT(m_cmd_buffer);
  }
#endif
}

} // namespace ren
