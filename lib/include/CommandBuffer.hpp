#pragma once
#include "Descriptors.hpp"
#include "Pipeline.hpp"
#include "Support/Optional.hpp"
#include "Support/Span.hpp"

namespace ren {

class Device;

struct RenderTargetConfig {
  TextureView rtv;
  VkAttachmentLoadOp load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
  VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_STORE;
  std::array<float, 4> clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
};

struct DepthStencilTargetConfig {
  TextureView dsv;
  VkAttachmentLoadOp depth_load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
  VkAttachmentStoreOp depth_store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  VkAttachmentLoadOp stencil_load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  VkAttachmentStoreOp stencil_store_op = VK_ATTACHMENT_STORE_OP_NONE;
  float clear_depth = 0.0f;
  uint8_t clear_stencil = 0;
};

class CommandBuffer {
  Device *m_device;
  VkCommandBuffer m_cmd_buffer;

public:
  CommandBuffer(Device *device, VkCommandBuffer cmd_buffer);

  VkCommandBuffer get() const { return m_cmd_buffer; }

  void begin();
  void end();

  void begin_rendering(
      int x, int y, unsigned width, unsigned height,
      std::span<const RenderTargetConfig> render_targets,
      const DepthStencilTargetConfig *depth_stencil_target = nullptr);

  void begin_rendering(const TextureRef &color_buffer) {
    RenderTargetConfig rt_cfg = {
        .rtv = TextureView::create(color_buffer,
                                   {.aspects = VK_IMAGE_ASPECT_COLOR_BIT})};
    begin_rendering(0, 0, color_buffer.desc.width, color_buffer.desc.height,
                    {&rt_cfg, 1});
  }

  void begin_rendering(const TextureRef &color_buffer,
                       const TextureRef &depth_buffer) {
    assert(color_buffer.desc.width == depth_buffer.desc.width);
    assert(color_buffer.desc.height == depth_buffer.desc.height);
    RenderTargetConfig rt = {
        .rtv = TextureView::create(color_buffer,
                                   {.aspects = VK_IMAGE_ASPECT_COLOR_BIT})};
    DepthStencilTargetConfig dst = {
        .dsv = TextureView::create(depth_buffer,
                                   {.aspects = VK_IMAGE_ASPECT_DEPTH_BIT}),
    };
    begin_rendering(0, 0, color_buffer.desc.width, color_buffer.desc.height,
                    {&rt, 1}, &dst);
  }

  void end_rendering();

  void copy_buffer(const BufferRef &src, const BufferRef &dst,
                   std::span<const VkBufferCopy> regions);

  void copy_buffer(const BufferRef &src, const BufferRef &dst,
                   const VkBufferCopy &region) {
    copy_buffer(src, dst, asSpan(region));
  }

  void copy_buffer_to_image(const BufferRef &src, const TextureRef &dst,
                            std::span<const VkBufferImageCopy> regions);

  void copy_buffer_to_image(const BufferRef &src, const TextureRef &dst,
                            const VkBufferImageCopy &region) {
    copy_buffer_to_image(src, dst, asSpan(region));
  }

  void blit(const TextureRef &src, const TextureRef &dst,
            std::span<const VkImageBlit> regions,
            VkFilter filter = VK_FILTER_LINEAR);

  void blit(const TextureRef &src, const TextureRef &dst,
            const VkImageBlit &region, VkFilter filter = VK_FILTER_LINEAR) {
    blit(src, dst, asSpan(region), filter);
  }

  void blit(const TextureRef &src, const TextureRef &dst) {
    VkImageBlit region = {
        .srcSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           .layerCount = 1},
        .srcOffsets = {{}, {int(src.desc.width), int(src.desc.height), 1}},
        .dstSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           .layerCount = 1},
        .dstOffsets = {{}, {int(dst.desc.width), int(dst.desc.height), 1}},
    };
    blit(src, dst, {&region, 1}, VK_FILTER_LINEAR);
  }

  void set_viewports(std::span<const VkViewport> viewports);
  void set_viewport(const VkViewport &viewport) {
    set_viewports({&viewport, 1});
  }

  void set_scissor_rects(std::span<const VkRect2D> rects);
  void set_scissor_rect(const VkRect2D &rect) { set_scissor_rects({&rect, 1}); }

  void bind_graphics_pipeline(GraphicsPipelineRef pipeline);

  void bind_descriptor_sets(VkPipelineBindPoint bind_point,
                            PipelineLayoutRef layout, unsigned first_set,
                            std::span<const VkDescriptorSet> sets);

  void set_push_constants(PipelineLayoutRef layout, VkShaderStageFlags stages,
                          std::span<const std::byte> data, unsigned offset = 0);

  void set_push_constants(PipelineLayoutRef layout, VkShaderStageFlags stages,
                          const auto &data, unsigned offset = 0) {
    set_push_constants(layout, stages, std::as_bytes(asSpan(data)), offset);
  }

  void bind_index_buffer(const BufferRef &buffer, VkIndexType format);

  void draw_indexed(unsigned num_indices, unsigned num_instances = 1,
                    unsigned first_index = 0, int vertex_offset = 0,
                    unsigned first_instance = 0);

  void pipeline_barrier(const VkDependencyInfo &dependency_info);

  void pipeline_barrier(std::span<const VkMemoryBarrier2> barriers,
                        std::span<const VkImageMemoryBarrier2> image_barriers) {
    VkDependencyInfo dependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = unsigned(barriers.size()),
        .pMemoryBarriers = barriers.data(),
        .imageMemoryBarrierCount = unsigned(image_barriers.size()),
        .pImageMemoryBarriers = image_barriers.data(),
    };
    pipeline_barrier(dependency);
  }
};

} // namespace ren
