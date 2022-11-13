#pragma once
#include "RenderGraph.hpp"

namespace ren {
class VulkanRenderGraphBuilder final : public RenderGraphBuilder {
  RGTextureID m_swapchain_image;
  RGSyncID m_acquire_semaphore;
  RGSyncID m_present_semaphore;

public:
  using RenderGraphBuilder::RenderGraphBuilder;

private:
  void addPresentNodes() override;
  std::unique_ptr<RenderGraph>
  createRenderGraph(Vector<Batch> batches,
                    HashMap<RGTextureID, Texture> textures,
                    HashMap<RGTextureID, RGTextureID> texture_aliases,
                    HashMap<RGSyncID, SyncObject> syncs) override;
  PassCallback
  generateBarrierGroup(std::span<const BarrierConfig> configs) override;
};

class VulkanRenderGraph final : public RenderGraph {
  RGTextureID m_swapchain_image;
  RGSyncID m_acquire_semaphore;
  RGSyncID m_present_semaphore;

  using RenderGraph::Batch;

public:
  VulkanRenderGraph(Swapchain *swapchain, Vector<Batch> batches,
                    HashMap<RGTextureID, Texture> textures,
                    HashMap<RGTextureID, RGTextureID> texture_aliases,
                    HashMap<RGSyncID, SyncObject> syncs,
                    RGTextureID swapchain_image, RGSyncID acquire_semaphore,
                    RGSyncID present_semaphore)
      : RenderGraph(swapchain, std::move(batches), std::move(textures),
                    std::move(texture_aliases), std::move(syncs)),
        m_swapchain_image(swapchain_image),
        m_acquire_semaphore(acquire_semaphore),
        m_present_semaphore(present_semaphore) {}

  void execute(CommandAllocator *cmd_pool) override;
};
} // namespace ren
