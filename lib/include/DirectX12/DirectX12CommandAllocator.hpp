#pragma once
#include "CommandAllocator.hpp"
#include "Support/ComPtr.hpp"

#include <d3d12.h>

namespace ren {
class DirectX12CommandAllocator final : public CommandAllocator {
private:
  void waitForFrame(uint64_t frame) override;
  void beginFrameImpl() override;
  void endFrameImpl() override;

public:
  DirectX12CommandAllocator(ComPtr<ID3D12Device> device,
                            uint64_t pipeline_depth);

  CommandBuffer *allocateCommandBuffer() override;
};
} // namespace ren
