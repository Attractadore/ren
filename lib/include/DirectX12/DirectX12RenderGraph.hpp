#pragma once
#include "RenderGraph.hpp"

namespace ren {
class DirectX12Device;

class DirectX12RenderGraph final : public RenderGraph {
  DirectX12Device *m_device;
  RGTextureID m_swapchain_buffer;

private:
  struct Config {
    DirectX12Device *device;
    RGTextureID swapchain_buffer;
  };

public:
  DirectX12RenderGraph(RenderGraph::Config base_config, Config config)
      : RenderGraph(std::move(base_config)), m_device(config.device),
        m_swapchain_buffer(config.swapchain_buffer) {}
  class Builder;

  void execute() override;
};

class DirectX12RenderGraph::Builder final : public RenderGraph::Builder {
  RGTextureID m_swapchain_buffer;

private:
  void addPresentNodes() override;

  RGCallback
  generateBarrierGroup(std::span<const BarrierConfig> configs) override;

  auto create_render_graph(RenderGraph::Config config)
      -> std::unique_ptr<RenderGraph> override;

public:
  using RenderGraph::Builder::Builder;
};
} // namespace ren
