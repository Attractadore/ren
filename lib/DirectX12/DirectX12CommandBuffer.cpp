#include "DirectX12/DirectX12CommandBuffer.hpp"
#include "DirectX12/Errors.hpp"

namespace ren {
DirectX12CommandBuffer::DirectX12CommandBuffer(
    DirectX12CommandAllocator *parent, ID3D12Device *device,
    ID3D12CommandAllocator *cmd_alloc)
    : m_parent(parent) {
  throwIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                          cmd_alloc, nullptr,
                                          IID_PPV_ARGS(&m_cmd_list)),
                "D3D12: Failed to create command list");
}

void DirectX12CommandBuffer::wait(SyncObject sync, PipelineStageFlags stages) {
  DIRECTX12_UNIMPLEMENTED;
}

void DirectX12CommandBuffer::signal(SyncObject sync,
                                    PipelineStageFlags stages) {
  DIRECTX12_UNIMPLEMENTED;
}

void DirectX12CommandBuffer::beginRendering(
    int x, int y, unsigned width, unsigned height,
    SmallVector<RenderTargetConfig, 8> render_targets,
    std::optional<DepthStencilTargetConfig> depth_stencil_target) {
  DIRECTX12_UNIMPLEMENTED;
}

void DirectX12CommandBuffer::endRendering() { DIRECTX12_UNIMPLEMENTED; }

void DirectX12CommandBuffer::blit(Texture src, Texture dst,
                                  std::span<const BlitRegion> regions,
                                  Filter filter) {
  DIRECTX12_UNIMPLEMENTED;
}

void DirectX12CommandBuffer::close() {
  throwIfFailed(m_cmd_list->Close(), "D3D12: Failed to record command list");
}

void DirectX12CommandBuffer::reset(ID3D12CommandAllocator *cmd_alloc) {
  throwIfFailed(m_cmd_list->Reset(cmd_alloc, nullptr),
                "D3D12: Failed to reset command list");
}
} // namespace ren
