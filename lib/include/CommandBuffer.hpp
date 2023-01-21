#pragma once
#include "Buffer.hpp"
#include "Descriptors.hpp"
#include "Formats.hpp"
#include "Pipeline.hpp"
#include "Support/Enum.hpp"
#include "Support/Optional.hpp"
#include "Support/Span.hpp"
#include "Support/Vector.hpp"
#include "Texture.hpp"

namespace ren {
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

struct CopyRegion {
  size_t src_offset;
  size_t dst_offset;
  size_t size;
};

struct Viewport {
  float x = 0.0f;
  float y = 0.0f;
  float width;
  float height;
  float min_depth = 0.0f;
  float max_depth = 1.0f;
};

struct ScissorRect {
  int x = 0;
  int y = 0;
  unsigned width;
  unsigned height;
};

class CommandBuffer {
public:
  virtual ~CommandBuffer() = default;

  virtual const Device &get_device() const = 0;
  virtual Device &get_device() = 0;

  virtual void beginRendering(
      int x, int y, unsigned width, unsigned height,
      std::span<const RenderTargetConfig> render_targets,
      const Optional<DepthStencilTargetConfig> &depth_stencil_target) = 0;
  void beginRendering(const RenderTargetView &rtv) {
    RenderTargetConfig rt_cfg = {.rtv = rtv};
    beginRendering(0, 0, rtv.texture.desc.width, rtv.texture.desc.height,
                   asSpan(rt_cfg), {});
  }
  void beginRendering(const TextureRef &texture) {
    RenderTargetConfig rt_cfg = {.rtv = RenderTargetView::create(texture)};
    beginRendering(0, 0, texture.desc.width, texture.desc.height,
                   asSpan(rt_cfg), {});
  }
  virtual void endRendering() = 0;

  virtual void set_viewports(std::span<const Viewport> viewports) = 0;
  void set_viewport(const Viewport &viewport) {
    set_viewports(asSpan(viewport));
  }

  virtual void set_scissor_rects(std::span<const ScissorRect> rects) = 0;
  void set_scissor_rect(const ScissorRect &rect) {
    set_scissor_rects(asSpan(rect));
  }

  virtual void bind_graphics_pipeline(GraphicsPipelineRef pipeline) = 0;

  virtual void
  bind_graphics_descriptor_sets(PipelineSignatureRef signature,
                                unsigned first_set,
                                std::span<const VkDescriptorSet> sets) = 0;
  void bind_graphics_descriptor_set(PipelineSignatureRef signature,
                                    unsigned first_set, VkDescriptorSet set) {
    bind_graphics_descriptor_sets(signature, first_set, asSpan(set));
  }

  virtual void set_graphics_push_constants(PipelineSignatureRef signature,
                                           VkShaderStageFlags stages,
                                           std::span<const std::byte> data,
                                           unsigned offset = 0) = 0;
  void set_graphics_push_constants(PipelineSignatureRef signature,
                                   VkShaderStageFlags stages, const auto &data,
                                   unsigned offset = 0) {
    set_graphics_push_constants(signature, stages, std::as_bytes(asSpan(data)),
                                offset);
  }

  virtual void bind_vertex_buffers(unsigned first_binding,
                                   std::span<const BufferRef> buffers) = 0;

  virtual void bind_index_buffer(const BufferRef &buffer,
                                 VkIndexType format = VK_INDEX_TYPE_UINT32) = 0;

  virtual void draw_indexed(unsigned num_indices, unsigned num_instances = 1,
                            unsigned first_index = 0, int vertex_offset = 0,
                            unsigned first_instance = 0) = 0;

  virtual void copy_buffer(const BufferRef &src, const BufferRef &dst,
                           std::span<const CopyRegion> regions) = 0;
  void copy_buffer(const BufferRef &src, const BufferRef &dst,
                   const CopyRegion &region) {
    copy_buffer(src, dst, {&region, 1});
  }

  virtual void close() = 0;
};
} // namespace ren
