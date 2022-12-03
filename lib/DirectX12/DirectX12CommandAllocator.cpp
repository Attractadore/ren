#include "DirectX12/DirectX12CommandAllocator.hpp"
#include "DirectX12/Errors.hpp"

namespace ren {
DirectX12CommandAllocator::DirectX12CommandAllocator(
    ComPtr<ID3D12Device> device, unsigned pipeline_depth) {
  DIRECTX12_UNIMPLEMENTED;
}

unsigned DirectX12CommandAllocator::getPipelineDepth() const {
  DIRECTX12_UNIMPLEMENTED;
}

CommandBuffer *DirectX12CommandAllocator::allocateCommandBuffer() {
  DIRECTX12_UNIMPLEMENTED;
}

void DirectX12CommandAllocator::beginFrame() { DIRECTX12_UNIMPLEMENTED; }

void DirectX12CommandAllocator::endFrame() { DIRECTX12_UNIMPLEMENTED; }
} // namespace ren
