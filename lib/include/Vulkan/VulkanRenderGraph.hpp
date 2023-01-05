#pragma once
#include "RenderGraph.hpp"

namespace ren {
class VulkanDevice;

class VulkanRenderGraph final : public RenderGraph {
  VulkanDevice *m_device;
  RGTextureID m_swapchain_image;
  RGSyncID m_acquire_semaphore;
  RGSyncID m_present_semaphore;

private:
  struct Config {
    VulkanDevice *device;
    RGTextureID swapchain_image;
    RGSyncID acquire_semaphore;
    RGSyncID present_semaphore;
  };

public:
  VulkanRenderGraph(RenderGraph::Config base_config, Config config)
      : RenderGraph(std::move(base_config)), m_device(config.device),
        m_swapchain_image(config.swapchain_image),
        m_acquire_semaphore(config.acquire_semaphore),
        m_present_semaphore(config.present_semaphore) {}
  class Builder;

  void execute() override;
};

class VulkanRenderGraph::Builder final : public RenderGraph::Builder {
  RGTextureID m_swapchain_image;
  RGSyncID m_acquire_semaphore;
  RGSyncID m_present_semaphore;

private:
  void addPresentNodes() override;
  auto create_render_graph(RenderGraph::Config config)
      -> std::unique_ptr<RenderGraph> override;
  RGCallback
  generateBarrierGroup(std::span<const BarrierConfig> configs) override;

public:
  Builder(VulkanDevice &device);
};
} // namespace ren
