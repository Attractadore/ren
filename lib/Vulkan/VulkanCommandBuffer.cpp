#include "Vulkan/VulkanCommandBuffer.hpp"
#include "Support/Views.hpp"
#include "Vulkan/VulkanBuffer.hpp"
#include "Vulkan/VulkanCommandAllocator.hpp"
#include "Vulkan/VulkanDescriptors.hpp"
#include "Vulkan/VulkanDevice.hpp"
#include "Vulkan/VulkanPipeline.hpp"
#include "Vulkan/VulkanPipelineStages.hpp"
#include "Vulkan/VulkanShaderStages.hpp"
#include "Vulkan/VulkanTexture.hpp"

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

const Device &VulkanCommandBuffer::get_device() const { return *m_device; }

Device &VulkanCommandBuffer::get_device() { return *m_device; }

void VulkanCommandBuffer::beginRendering(
    int x, int y, unsigned width, unsigned height,
    SmallVector<RenderTargetConfig, 8> render_targets,
    Optional<DepthStencilTargetConfig> depth_stencil_target) {

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

void VulkanCommandBuffer::copy_buffer(const BufferRef &src,
                                      const BufferRef &dst,
                                      std::span<const CopyRegion> regions) {
  auto vk_regions = map(regions,
                        [](const CopyRegion &region) {
                          return VkBufferCopy{
                              .srcOffset = region.src_offset,
                              .dstOffset = region.dst_offset,
                              .size = region.size,
                          };
                        }) |
                    ranges::to<SmallVector<VkBufferCopy, 8>>;
  m_device->CmdCopyBuffer(m_cmd_buffer, getVkBuffer(src), getVkBuffer(dst),
                          vk_regions.size(), vk_regions.data());
}

void VulkanCommandBuffer::blit(VkImage src, VkImage dst,
                               std::span<const VkImageBlit> regions,
                               VkFilter filter) {
  m_device->CmdBlitImage(m_cmd_buffer, src,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, regions.size(),
                         regions.data(), filter);
}

void VulkanCommandBuffer::set_viewports(std::span<const Viewport> viewports) {
  auto vk_viewports = viewports | map([](const Viewport &viewport) {
                        return VkViewport{
                            .x = viewport.x,
                            .y = viewport.y + viewport.height,
                            .width = viewport.width,
                            .height = -viewport.height,
                            .minDepth = viewport.min_depth,
                            .maxDepth = viewport.max_depth,
                        };
                      }) |
                      ranges::to<SmallVector<VkViewport, 8>>;
  m_device->CmdSetViewportWithCount(m_cmd_buffer, vk_viewports.size(),
                                    vk_viewports.data());
}

void VulkanCommandBuffer::set_scissor_rects(
    std::span<const ScissorRect> rects) {
  auto vk_rects = rects | map([](const ScissorRect &rect) {
                    return VkRect2D{
                        .offset = {rect.x, rect.y},
                        .extent = {rect.width, rect.height},
                    };
                  }) |
                  ranges::to<SmallVector<VkRect2D, 8>>;
  m_device->CmdSetScissorWithCount(m_cmd_buffer, vk_rects.size(),
                                   vk_rects.data());
}

void VulkanCommandBuffer::bind_graphics_pipeline(GraphicsPipelineRef pipeline) {
  m_device->CmdBindPipeline(m_cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            getVkPipeline(pipeline));
}

void VulkanCommandBuffer::bind_graphics_descriptor_sets(
    PipelineSignatureRef signature, unsigned first_set,
    std::span<const DescriptorSet> sets) {
  auto vk_sets =
      sets | map(getVkDescriptorSet) | ranges::to<SmallVector<VkDescriptorSet>>;
  m_device->CmdBindDescriptorSets(m_cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  getVkPipelineLayout(signature), first_set,
                                  vk_sets.size(), vk_sets.data(), 0, nullptr);
}

void VulkanCommandBuffer::set_graphics_push_constants(
    PipelineSignatureRef signature, ShaderStageFlags stages,
    std::span<const std::byte> data, unsigned offset) {
  assert(not stages.isSet(ShaderStage::Compute));
  m_device->CmdPushConstants(m_cmd_buffer, getVkPipelineLayout(signature),
                             getVkShaderStageFlags(stages), offset, data.size(),
                             data.data());
}

void VulkanCommandBuffer::bind_vertex_buffers(
    unsigned first_binding, std::span<const BufferRef> buffers) {
  auto vk_buffers =
      buffers | map(getVkBuffer) | ranges::to<SmallVector<VkBuffer, 32>>;
  auto offsets = buffers |
                 map([](BufferRef buffer) { return buffer.desc.offset; }) |
                 ranges::to<SmallVector<VkDeviceSize, 32>>;
  auto sizes = buffers |
               map([](BufferRef buffer) { return buffer.desc.size; }) |
               ranges::to<SmallVector<VkDeviceSize, 32>>;
  m_device->CmdBindVertexBuffers2(m_cmd_buffer, first_binding, buffers.size(),
                                  vk_buffers.data(), offsets.data(),
                                  sizes.data(), nullptr);
}

void VulkanCommandBuffer::bind_index_buffer(const BufferRef &buffer,
                                            IndexFormat format) {
  m_device->CmdBindIndexBuffer(m_cmd_buffer, getVkBuffer(buffer),
                               buffer.desc.offset, getVkIndexType(format));
}

void VulkanCommandBuffer::draw_indexed(unsigned num_indices,
                                       unsigned num_instances,
                                       unsigned first_index, int vertex_offset,
                                       unsigned first_instance) {
  m_device->CmdDrawIndexed(m_cmd_buffer, num_indices, num_instances,
                           first_index, vertex_offset, first_instance);
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
