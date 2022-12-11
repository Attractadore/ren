#include "DirectX12/DirectX12RenderGraph.hpp"
#include "DirectX12/DirectX12CommandAllocator.hpp"
#include "DirectX12/DirectX12CommandBuffer.hpp"
#include "DirectX12/DirectX12Device.hpp"
#include "DirectX12/DirectX12Swapchain.hpp"
#include "DirectX12/DirectX12Texture.hpp"
#include "DirectX12/Errors.hpp"
#include "Support/Errors.hpp"
#include "Support/Math.hpp"
#include "Support/Views.hpp"
#include "hlsl/Texture2DBlitConfig.hlsl"

namespace ren {
void DirectX12RenderGraph::Builder::addPresentNodes() {
  auto *dx_swapchain = static_cast<DirectX12Swapchain *>(m_swapchain);

  auto acquire = addNode();
  acquire.setDesc("D3D12: Acquire swapchain buffer");
  m_swapchain_buffer =
      acquire.addExternalTextureOutput({}, PipelineStage::Present);
  setDesc(m_swapchain_buffer, "D3D12: Swapchain buffer");

  auto blit = addNode();
  blit.setDesc("D3D12: Blit final texture to swapchain");
  blit.addReadInput(m_final_image, MemoryAccess::SampledRead,
                    PipelineStage::Compute);
  auto blitted_swapchain_buffer = blit.addWriteInput(
      m_swapchain_buffer, MemoryAccess::StorageWrite, PipelineStage::Compute);
  setDesc(blitted_swapchain_buffer, "D3D12: Blitted swapchain buffer");
  blit.setCallback(
      [final_texture = m_final_image, swapchain_buffer = m_swapchain_buffer,
       root_sig = dx_swapchain->getBlitRootSignature(),
       pso = dx_swapchain->getBlitPSO()](CommandBuffer &cmd, RenderGraph &rg) {
        auto *dx_cmd = static_cast<DirectX12CommandBuffer *>(&cmd);
        auto *dx_device = dx_cmd->getDevice();
        auto *dx_cmd_alloc = dx_cmd->getParent();
        auto *cmd_list = dx_cmd->get();
        auto &src_tex = rg.getTexture(final_texture);
        auto &dst_tex = rg.getTexture(swapchain_buffer);
        SampledTextureView src_srv = {
            .desc = {.mip_levels = 1},
            .texture = src_tex,
        };
        StorageTextureView dst_uav = {.texture = dst_tex};
        std::array cpu_descriptors = {
            dx_device->getSRV(src_srv).cpu_handle,
            dx_device->getUAV(dst_uav).cpu_handle,
        };
        UINT srv_uav_table_size = cpu_descriptors.size();
        Descriptor srv_uav_table =
            dx_cmd_alloc->allocateDescriptors(srv_uav_table_size);
        dx_device->get()->CopyDescriptors(
            1, &srv_uav_table.cpu_handle, &srv_uav_table_size,
            cpu_descriptors.size(), cpu_descriptors.data(), nullptr,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        cmd_list->SetComputeRootSignature(root_sig);
        cmd_list->SetPipelineState(pso);
        cmd_list->SetComputeRootDescriptorTable(0, srv_uav_table.gpu_handle);
        Texture2DBlitConfig config = {
            .src_texel_size =
                1.0f / glm::vec2(src_tex.desc.width, src_tex.desc.height)};
        cmd_list->SetComputeRoot32BitConstants(
            1, sizeof(config) / sizeof(uint32_t), &config, 0);
        cmd_list->Dispatch(ceilDiv(dst_tex.desc.width, BlitTexture2DThreadsX),
                           ceilDiv(dst_tex.desc.height, BlitTexture2DThreadsY),
                           1);
        dx_cmd_alloc->addFrameResource(std::move(src_srv));
        dx_cmd_alloc->addFrameResource(std::move(dst_uav));
      });

  auto present = addNode();
  present.setDesc(
      "D3D12: Transition swapchain buffer to D3D12_RESOURCE_STATE_PRESENT");
  present.addReadInput(blitted_swapchain_buffer, {}, PipelineStage::Present);
}

namespace {
D3D12_RESOURCE_STATES
getD3D12ResourceStateFromAccessesAndStages(MemoryAccessFlags accesses,
                                           PipelineStageFlags stages) {
  using enum MemoryAccess;
  using enum PipelineStage;
  if (accesses.isSet(ColorWrite)) {
    return D3D12_RESOURCE_STATE_RENDER_TARGET;
  } else if (accesses.isSet(TransferRead)) {
    return D3D12_RESOURCE_STATE_COPY_SOURCE;
  } else if (accesses.isSet(TransferWrite)) {
    return D3D12_RESOURCE_STATE_COPY_DEST;
  } else if (accesses.isSet(StorageRead) or accesses.isSet(StorageWrite)) {
    return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  } else if (stages.isSet(Present)) {
    return D3D12_RESOURCE_STATE_PRESENT;
  } else if (accesses == SampledRead) {
    return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  }
  return D3D12_RESOURCE_STATE_COMMON;
}
} // namespace

RGCallback DirectX12RenderGraph::Builder::generateBarrierGroup(
    std::span<const BarrierConfig> configs) {
  auto textures = configs |
                  ranges::views::transform([](const BarrierConfig &config) {
                    return config.texture;
                  }) |
                  ranges::to<SmallVector<RGTextureID, 8>>;

  auto barriers =
      configs | map([](const BarrierConfig &config) {
        return D3D12_RESOURCE_BARRIER{
            .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
            .Transition =
                {
                    .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                    .StateBefore = getD3D12ResourceStateFromAccessesAndStages(
                        config.src_accesses, config.src_stages),
                    .StateAfter = getD3D12ResourceStateFromAccessesAndStages(
                        config.dst_accesses, config.dst_stages),
                },
        };
      }) |
      ranges::to<SmallVector<D3D12_RESOURCE_BARRIER, 8>>;

  return [textures = std::move(textures), barriers = std::move(barriers)](
             CommandBuffer &cmd, RenderGraph &rg) mutable {
    for (auto &&[tex, barrier] : ranges::views::zip(textures, barriers)) {
      barrier.Transition.pResource = getD3D12Resource(rg.getTexture(tex));
    }
    auto *dx_cmd = static_cast<DirectX12CommandBuffer *>(&cmd);
    auto *cmd_list = dx_cmd->get();
    cmd_list->ResourceBarrier(barriers.size(), barriers.data());
  };
}

std::unique_ptr<RenderGraph> DirectX12RenderGraph::Builder::createRenderGraph(
    Vector<Batch> batches, Vector<Texture> textures,
    HashMap<RGTextureID, unsigned> phys_textures, Vector<SyncObject> syncs) {
  return std::make_unique<DirectX12RenderGraph>(
      m_swapchain, std::move(batches), std::move(textures),
      std::move(phys_textures), std::move(syncs), m_swapchain_buffer);
}

void DirectX12RenderGraph::execute(CommandAllocator *cmd_alloc) {
  auto *dx_cmd_alloc = static_cast<DirectX12CommandAllocator *>(cmd_alloc);
  auto *dx_device = dx_cmd_alloc->getDevice();
  auto *dx_swapchain = static_cast<DirectX12Swapchain *>(m_swapchain);

  dx_swapchain->AcquireBuffer(*dx_cmd_alloc);
  setTexture(m_swapchain_buffer, dx_swapchain->getTexture());

  auto *queue = dx_device->getDirectQueue();
  SmallVector<ID3D12CommandList *, 16> cmd_lists;
  for (const auto &batch : m_batches) {
    for (auto &&[barrier, pass] :
         ranges::views::zip(batch.barrier_cbs, batch.pass_cbs)) {
      auto *dx_cmd = dx_cmd_alloc->allocateDirectX12CommandBuffer();
      if (barrier) {
        barrier(*dx_cmd, *this);
      }
      if (pass) {
        pass(*dx_cmd, *this);
      }
      dx_cmd->close();
      cmd_lists.push_back(dx_cmd->get());
    }

    queue->ExecuteCommandLists(cmd_lists.size(), cmd_lists.data());
    cmd_lists.clear();
  }

  dx_swapchain->PresentBuffer();
};
} // namespace ren
