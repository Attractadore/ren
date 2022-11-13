#pragma once
#include "PipelineStages.hpp"
#include "Support/Span.hpp"
#include "Support/Vector.hpp"
#include "Sync.hpp"
#include "Texture.hpp"

#include <optional>

namespace ren {
enum class RenderTargetLoadOp {
  Clear,
  Load,
  Discard,
};

enum class RenderTargetStoreOp {
  Store,
  Discard,
};

struct RenderTargetConfig {
  TextureView view;
  RenderTargetLoadOp load_op = RenderTargetLoadOp::Clear;
  RenderTargetStoreOp store_op = RenderTargetStoreOp::Store;
  std::array<float, 4> clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
};

struct DepthRenderTargetConfig {
  TextureView view;
  RenderTargetLoadOp load_op = RenderTargetLoadOp::Clear;
  RenderTargetStoreOp store_op = RenderTargetStoreOp::Store;
  float clear_depth = 0.0f;
};

struct StencilRenderTargetConfig {
  TextureView view;
  RenderTargetLoadOp load_op = RenderTargetLoadOp::Clear;
  RenderTargetStoreOp store_op = RenderTargetStoreOp::Store;
  uint32_t clear_stencil;
};

struct BlitRegion {
  unsigned src_mip_level = 0;
  unsigned dst_mip_level = 0;
  unsigned src_first_array_layer = 0;
  unsigned dst_first_array_layer = 0;
  unsigned array_layer_count = 1;
  unsigned src_offsets[2][3];
  unsigned dst_offsets[2][3];
};

enum class Filter {
  Nearest,
  Linear,
};

class CommandBuffer {
public:
  virtual ~CommandBuffer() = default;

  virtual void wait(SyncObject sync, PipelineStageFlags stages) = 0;
  virtual void signal(SyncObject sync, PipelineStageFlags stages) = 0;

  virtual void beginRendering(
      int x, int y, unsigned width, unsigned height,
      SmallVector<RenderTargetConfig, 8> render_targets,
      std::optional<DepthRenderTargetConfig> depth_render_target,
      std::optional<StencilRenderTargetConfig> stencil_render_target) = 0;
  void beginRendering(Texture render_target) {
    beginRendering(0, 0, render_target.desc.width, render_target.desc.height,
                   {{.view = {.texture = std::move(render_target)}}}, {}, {});
  }
  virtual void endRendering() = 0;

  virtual void blit(Texture src, Texture dst,
                    std::span<const BlitRegion> regions, Filter filter) = 0;
  void blit(Texture src, Texture dst) {
    BlitRegion region = {
        .src_offsets = {{}, {src.desc.width, src.desc.height, 1}},
        .dst_offsets = {{}, {dst.desc.width, dst.desc.height, 1}},
    };
    blit(std::move(src), std::move(dst), asSpan(region), Filter::Linear);
  }

  virtual void close() = 0;
};
} // namespace ren
