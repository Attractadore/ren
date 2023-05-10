#pragma once
#include "Descriptors.hpp"
#include "Pipeline.hpp"
#include "Support/Optional.hpp"
#include "Support/Span.hpp"

namespace ren {

class Device;

struct ColorAttachment {
  TextureView texture;
  VkAttachmentLoadOp load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
  VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_STORE;
  glm::vec4 clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
};

struct DepthStencilAttachment {
  struct Depth {
    VkAttachmentLoadOp load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
    VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    float clear_depth = 0.0f;
  };

  struct Stencil {
    VkAttachmentLoadOp load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
    VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_STORE;
    uint8_t clear_stencil = 0;
  };

  TextureView texture;
  Optional<Depth> depth;
  Optional<Stencil> stencil;
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
      std::span<const Optional<ColorAttachment>> color_attachments,
      Optional<const DepthStencilAttachment &> depth_stencil_attachment = None);

  void begin_rendering(TextureView color_attachment);

  void begin_rendering(TextureView color_attachment,
                       TextureView depth_attachment);

  void end_rendering();

  void copy_buffer(const Buffer &src, const Buffer &dst,
                   std::span<const VkBufferCopy> regions);

  void copy_buffer(const Buffer &src, const Buffer &dst,
                   const VkBufferCopy &region);

  void copy_buffer(const BufferView &src, const BufferView &dst);

  void copy_buffer_to_image(const Buffer &src, const Texture &dst,
                            std::span<const VkBufferImageCopy> regions);

  void copy_buffer_to_image(const Buffer &src, const Texture &dst,
                            const VkBufferImageCopy &region) {
    copy_buffer_to_image(src, dst, asSpan(region));
  }

  void blit(const Texture &src, const Texture &dst,
            std::span<const VkImageBlit> regions, VkFilter filter);

  void blit(const Texture &src, const Texture &dst, const VkImageBlit &region,
            VkFilter filter);

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

  void bind_index_buffer(const BufferView &buffer, VkIndexType type);

  struct DrawIndexedInfo {
    unsigned num_indices = 0;
    unsigned num_instances = 1;
    unsigned first_index = 0;
    int vertex_offset = 0;
    unsigned first_instance = 0;
  };

  void draw_indexed(const DrawIndexedInfo &&draw_info);

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
