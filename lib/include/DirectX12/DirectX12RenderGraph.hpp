#pragma once
#include "RenderGraph.hpp"

namespace ren {
class DirectX12Device;

class DirectX12RenderGraph final : public RenderGraph {
  DirectX12Device *m_device;
  RGTextureID m_swapchain_buffer;

public:
  struct Config {
    DirectX12Device *device;
    RGTextureID swapchain_buffer;
  };

  class Builder;
  DirectX12RenderGraph(RenderGraph::Config base_config, Config config)
      : RenderGraph(std::move(base_config)), m_device(config.device),
        m_swapchain_buffer(config.swapchain_buffer) {}

  void execute() override;
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
