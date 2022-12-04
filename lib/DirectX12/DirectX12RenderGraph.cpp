#include "DirectX12/DirectX12RenderGraph.hpp"
#include "DirectX12/DirectX12CommandBuffer.hpp"
#include "DirectX12/Errors.hpp"

namespace ren {
void DirectX12RenderGraph::Builder::addPresentNodes() {
  auto acquire = addNode();
  acquire.setDesc("D3D12: Acquire swapchain surface");
  m_swapchain_surface =
      acquire.addExternalTextureOutput({}, PipelineStage::Present);
  setDesc(m_swapchain_surface, "D3D12: Swapchain surface");

  auto blit = addNode();
  blit.setDesc("D3D12: Blit final image to swapchain");
  blit.addReadInput(m_final_image, MemoryAccess::TransferRead,
                    PipelineStage::Blit);
  auto blitted_swapchain_surface = blit.addWriteInput(
      m_swapchain_surface, MemoryAccess::TransferWrite, PipelineStage::Blit);
  setDesc(blitted_swapchain_surface, "D3D12: Blitted swapchain surface");
  blit.setCallback([final_image = m_final_image,
                    swapchain_surface = m_swapchain_surface](CommandBuffer &cmd,
                                                             RenderGraph &rg) {
    auto &src_tex = rg.getTexture(final_image);
    auto &dst_tex = rg.getTexture(swapchain_surface);
    cmd.blit(src_tex, dst_tex);
  });

  auto present = addNode();
  present.setDesc(
      "D3D12: Transition swapchain surface to D3D12_RESOURCE_STATE_PRESENT");
  present.addReadInput(blitted_swapchain_surface, {}, PipelineStage::Present);
}

RGCallback DirectX12RenderGraph::Builder::generateBarrierGroup(
    std::span<const BarrierConfig> configs) {
  DIRECTX12_UNIMPLEMENTED;
}

std::unique_ptr<RenderGraph> DirectX12RenderGraph::Builder::createRenderGraph(
    Vector<Batch> batches, Vector<Texture> textures,
    HashMap<RGTextureID, unsigned> phys_textures, Vector<SyncObject> syncs) {
  DIRECTX12_UNIMPLEMENTED;
}

void DirectX12RenderGraph::execute(CommandAllocator *cmd_alloc) {
  DIRECTX12_UNIMPLEMENTED;
};
} // namespace ren
