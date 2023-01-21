#pragma once
#include "CommandBuffer.hpp"
#include "Support/Vector.hpp"
#include "VulkanErrors.hpp"

namespace ren {

class VulkanDevice;
class VulkanCommandAllocator;

class VulkanCommandBuffer final : public CommandBuffer {
  VulkanDevice *m_device;
  VkCommandBuffer m_cmd_buffer;
  VulkanCommandAllocator *m_parent;

public:
  VulkanCommandBuffer(VulkanDevice *device, VkCommandBuffer cmd_buffer,
                      VulkanCommandAllocator *parent);

  const Device &get_device() const override;
  Device &get_device() override;

  void beginRendering(
      int x, int y, unsigned width, unsigned height,
      std::span<const RenderTargetConfig> render_targets,
      const Optional<DepthStencilTargetConfig> &depth_stencil_target) override;
  void endRendering() override;

  void copy_buffer(const BufferRef &src, const BufferRef &dst,
                   std::span<const CopyRegion> regions) override;

  void blit(VkImage src, VkImage dst, std::span<const VkImageBlit> regions,
            VkFilter filter);
  void blit(const TextureRef &src, const TextureRef &dst) {
    VkImageBlit region = {
        .srcSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           .layerCount = 1},
        .srcOffsets = {{}, {int(src.desc.width), int(src.desc.height), 1}},
        .dstSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           .layerCount = 1},
        .dstOffsets = {{}, {int(dst.desc.width), int(dst.desc.height), 1}},
    };
    blit(src.handle, dst.handle, asSpan(region), VK_FILTER_LINEAR);
  }

  void set_viewports(std::span<const Viewport> viewports) override;

  void set_scissor_rects(std::span<const ScissorRect> rects) override;

  void bind_graphics_pipeline(GraphicsPipelineRef pipeline) override;

  void
  bind_graphics_descriptor_sets(PipelineLayoutRef signature, unsigned first_set,
                                std::span<const VkDescriptorSet> sets) override;

  void set_graphics_push_constants(PipelineLayoutRef signature,
                                   VkShaderStageFlags stages,
                                   std::span<const std::byte> data,
                                   unsigned offset) override;

  void bind_vertex_buffers(unsigned first_binding,
                           std::span<const BufferRef> buffers) override;

  void bind_index_buffer(const BufferRef &buffer, VkIndexType format) override;

  void draw_indexed(unsigned num_indices, unsigned num_instances,
                    unsigned first_index, int vertex_offset,
                    unsigned first_instance) override;

  void close() override;

  VulkanDevice *getDevice() { return m_device; }
  VkCommandBuffer get() { return m_cmd_buffer; }
};
} // namespace ren
