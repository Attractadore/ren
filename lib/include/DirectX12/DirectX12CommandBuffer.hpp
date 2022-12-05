#pragma once
#include "CommandBuffer.hpp"
#include "Support/ComPtr.hpp"

#include <d3d12.h>

namespace ren {
class DirectX12CommandAllocator;

class DirectX12CommandBuffer final : public CommandBuffer {
  ComPtr<ID3D12GraphicsCommandList> m_cmd_list;
  DirectX12CommandAllocator *m_parent;

public:
  DirectX12CommandBuffer(DirectX12CommandAllocator *parent,
                         ID3D12Device *device,
                         ID3D12CommandAllocator *cmd_alloc);
  void wait(SyncObject sync, PipelineStageFlags stages) override;
  void signal(SyncObject sync, PipelineStageFlags stages) override;

  void beginRendering(
      int x, int y, unsigned width, unsigned height,
      SmallVector<RenderTargetConfig, 8> render_targets,
      std::optional<DepthRenderTargetConfig> depth_render_target,
      std::optional<StencilRenderTargetConfig> stencil_render_target) override;
  void endRendering() override;

  void blit(Texture src, Texture dst, std::span<const BlitRegion> regions,
            Filter filter) override;

  void close() override;
  void reset(ID3D12CommandAllocator *command_allocator);

  ID3D12GraphicsCommandList* get() { return m_cmd_list.Get(); }
};
} // namespace ren
