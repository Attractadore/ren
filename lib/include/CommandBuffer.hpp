#pragma once
#include "Buffer.hpp"
#include "Descriptors.hpp"
#include "Formats.hpp"
#include "Pipeline.hpp"
#include "PipelineStages.hpp"
#include "Support/Enum.hpp"
#include "Support/Optional.hpp"
#include "Support/Span.hpp"
#include "Support/Vector.hpp"
#include "Sync.hpp"
#include "Texture.hpp"

namespace ren {
#define REN_RENDER_TARGET_LOAD_OPS (Clear)(Load)(Discard)(None)
REN_DEFINE_ENUM(TargetLoadOp, REN_RENDER_TARGET_LOAD_OPS);

#define REN_RENDER_TARGET_STORE_OPS (Store)(Discard)(None)
REN_DEFINE_ENUM(TargetStoreOp, REN_RENDER_TARGET_STORE_OPS);

struct RenderTargetConfig {
  RenderTargetView rtv;
  TargetLoadOp load_op = TargetLoadOp::Clear;
  TargetStoreOp store_op = TargetStoreOp::Store;
  std::array<float, 4> clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
};

struct DepthStencilTargetConfig {
  DepthStencilView dsv;
  TargetLoadOp depth_load_op = TargetLoadOp::Clear;
  TargetStoreOp depth_store_op = TargetStoreOp::Store;
  TargetLoadOp stencil_load_op = TargetLoadOp::None;
  TargetStoreOp stencil_store_op = TargetStoreOp::None;
  float clear_depth = 0.0f;
  uint8_t clear_stencil = 0;
};

#define REN_FILTERS (Nearest)(Linear)
REN_DEFINE_ENUM(Filter, REN_FILTERS);

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

  virtual void beginRendering(int x, int y, unsigned width, unsigned height,
                              SmallVector<RenderTargetConfig, 8> rts,
                              Optional<DepthStencilTargetConfig> dst) = 0;
  void beginRendering(Texture rt) {
    beginRendering(0, 0, rt.desc.width, rt.desc.height,
                   {{.rtv = {.texture = std::move(rt)}}}, {});
  }
  void beginRendering(RenderTargetView rtv) {
    beginRendering(0, 0, rtv.texture.desc.width, rtv.texture.desc.height,
                   {{.rtv = std::move(rtv)}}, {});
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
                                std::span<const DescriptorSet> sets) = 0;
  void bind_graphics_descriptor_set(PipelineSignatureRef signature,
                                    unsigned first_set, DescriptorSet set) {
    bind_graphics_descriptor_sets(signature, first_set, asSpan(set));
  }

  virtual void set_graphics_push_constants(
      PipelineSignatureRef signature, ShaderStageFlags stages,
      std::span<const std::byte> data, unsigned offset = 0) = 0;
  void set_graphics_push_constants(PipelineSignatureRef signature,
                                   ShaderStageFlags stages, const auto &data,
                                   unsigned offset = 0) {
    set_graphics_push_constants(signature, stages, std::as_bytes(asSpan(data)),
                                offset);
  }

  virtual void bind_vertex_buffers(unsigned first_binding,
                                   std::span<const BufferRef> buffers) = 0;

  virtual void bind_index_buffer(const BufferRef &buffer,
                                 IndexFormat format = IndexFormat::U32) = 0;

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
