#pragma once
#include "RenderGraph.hpp"

namespace ren {
class DirectX12RenderGraph final : public RenderGraph {
  RGTextureID m_swapchain_buffer;

public:
  DirectX12RenderGraph(Swapchain *swapchain, Vector<Batch> batches,
                       Vector<Texture> textures,
                       HashMap<RGTextureID, unsigned> phys_textures,
                       Vector<SyncObject> syncs, RGTextureID swapchain_buffer)
      : RenderGraph(swapchain, std::move(batches), std::move(textures),
                    std::move(phys_textures), std::move(syncs)),
        m_swapchain_buffer(swapchain_buffer) {}

  class Builder;
  void execute(CommandAllocator *cmd_pool) override;
};

class DirectX12RenderGraph::Builder final : public RenderGraph::Builder {
  RGTextureID m_swapchain_buffer;

private:
  void addPresentNodes() override;

  RGCallback
  generateBarrierGroup(std::span<const BarrierConfig> configs) override;

  std::unique_ptr<RenderGraph>
  createRenderGraph(Vector<Batch> batches, Vector<Texture> textures,
                    HashMap<RGTextureID, unsigned> phys_textures,
                    Vector<SyncObject> syncs) override;

public:
  using RenderGraph::Builder::Builder;
};
} // namespace ren
