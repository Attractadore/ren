#pragma once
#include "Descriptors.hpp"
#include "Pipeline.hpp"
#include "Support/Optional.hpp"
#include "Support/Span.hpp"

namespace ren {

class VulkanDevice;

struct RenderTargetConfig {
  RenderTargetView rtv;
  VkAttachmentLoadOp load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
  VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_STORE;
  std::array<float, 4> clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
};

struct DepthStencilTargetConfig {
  DepthStencilView dsv;
  VkAttachmentLoadOp depth_load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
  VkAttachmentStoreOp depth_store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  VkAttachmentLoadOp stencil_load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  VkAttachmentStoreOp stencil_store_op = VK_ATTACHMENT_STORE_OP_NONE;
  float clear_depth = 0.0f;
  uint8_t clear_stencil = 0;
};

class CommandBuffer {
  VulkanDevice *m_device;
  VkCommandBuffer m_cmd_buffer;

public:
  CommandBuffer(VulkanDevice *device, VkCommandBuffer cmd_buffer);

  const Device &get_device() const;
  Device &get_device();

  VkCommandBuffer get() const { return m_cmd_buffer; }

  void begin();
  void end();

  void begin_rendering(
      int x, int y, unsigned width, unsigned height,
      std::span<const RenderTargetConfig> render_targets,
      const Optional<DepthStencilTargetConfig> &depth_stencil_target);

  void begin_rendering(const RenderTargetView &rtv) {
    RenderTargetConfig rt_cfg = {.rtv = rtv};
    begin_rendering(0, 0, rtv.texture.desc.width, rtv.texture.desc.height,
                    {&rt_cfg, 1}, {});
  }

  void begin_rendering(const TextureRef &texture) {
    RenderTargetConfig rt_cfg = {.rtv = RenderTargetView::create(texture)};
    begin_rendering(0, 0, texture.desc.width, texture.desc.height, {&rt_cfg, 1},
                    {});
  }

  void end_rendering();

  void copy_buffer(const BufferRef &src, const BufferRef &dst,
                   std::span<const VkBufferCopy> regions);

  void blit(const TextureRef &src, const TextureRef &dst,
            std::span<const VkImageBlit> regions, VkFilter filter);
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

  void set_viewports(SmallVector<VkViewport, 8> viewports);
  void set_viewport(const VkViewport &viewport) { set_viewports({viewport}); }

  void set_scissor_rects(std::span<const VkRect2D> rects);
  void set_scissor_rect(const VkRect2D &rect) { set_scissor_rects({&rect, 1}); }

  void bind_graphics_pipeline(GraphicsPipelineRef pipeline);

  void bind_graphics_descriptor_sets(PipelineLayoutRef layout,
                                     unsigned first_set,
                                     std::span<const VkDescriptorSet> sets);

  void set_push_constants(PipelineLayoutRef layout, VkShaderStageFlags stages,
                          std::span<const std::byte> data, unsigned offset);

  void set_push_constants(PipelineLayoutRef layout, VkShaderStageFlags stages,
                          const auto &data, unsigned offset = 0) {
    set_push_constants(layout, stages, std::as_bytes(asSpan(data)), offset);
  }

  void bind_index_buffer(const BufferRef &buffer, VkIndexType format);

  void draw_indexed(unsigned num_indices, unsigned num_instances = 1,
                    unsigned first_index = 0, int vertex_offset = 0,
                    unsigned first_instance = 0);
};

} // namespace ren
