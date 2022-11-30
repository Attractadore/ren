#pragma once
#include "RenderGraph.hpp"

namespace ren {
class VulkanRenderGraph final : public RenderGraph {
  RGTextureID m_swapchain_image;
  RGSyncID m_acquire_semaphore;
  RGSyncID m_present_semaphore;

public:
  class Builder;
  VulkanRenderGraph(Swapchain *swapchain, Vector<Batch> batches,
                    Vector<Texture> textures,
                    HashMap<RGTextureID, unsigned> phys_textures,
                    Vector<SyncObject> syncs, RGTextureID swapchain_image,
                    RGSyncID acquire_semaphore, RGSyncID present_semaphore)
      : RenderGraph(swapchain, std::move(batches), std::move(textures),
                    std::move(phys_textures), std::move(syncs)),
        m_swapchain_image(swapchain_image),
        m_acquire_semaphore(acquire_semaphore),
        m_present_semaphore(present_semaphore) {}

  void execute(CommandAllocator *cmd_pool) override;
};

class VulkanRenderGraph::Builder final : public RenderGraph::Builder {
  RGTextureID m_swapchain_image;
  RGSyncID m_acquire_semaphore;
  RGSyncID m_present_semaphore;

public:
  using RenderGraph::Builder::Builder;

private:
  void addPresentNodes() override;
  std::unique_ptr<RenderGraph>
  createRenderGraph(Vector<Batch> batches, Vector<Texture> textures,
                    HashMap<RGTextureID, unsigned> phys_textures,
                    Vector<SyncObject> syncs) override;
  RGCallback
  generateBarrierGroup(std::span<const BarrierConfig> configs) override;
};
} // namespace ren
