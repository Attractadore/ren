#include "DirectX12/DirectX12CommandAllocator.hpp"
#include "DirectX12/Errors.hpp"

namespace ren {
DirectX12CommandAllocator::DirectX12CommandAllocator(
    ComPtr<ID3D12Device> device, uint64_t pipeline_depth)
    : CommandAllocator(pipeline_depth) {
  DIRECTX12_UNIMPLEMENTED;
}

CommandBuffer *DirectX12CommandAllocator::allocateCommandBuffer() {
  DIRECTX12_UNIMPLEMENTED;
}

void DirectX12CommandAllocator::waitForFrame(uint64_t frame) {
  DIRECTX12_UNIMPLEMENTED;
}

void DirectX12CommandAllocator::beginFrameImpl() { DIRECTX12_UNIMPLEMENTED; }

void DirectX12CommandAllocator::endFrameImpl() { DIRECTX12_UNIMPLEMENTED; }
} // namespace ren
