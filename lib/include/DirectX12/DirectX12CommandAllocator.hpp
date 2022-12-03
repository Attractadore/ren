#pragma once
#include "CommandAllocator.hpp"
#include "Support/ComPtr.hpp"

#include <d3d12.h>

namespace ren {
class DirectX12CommandAllocator final : public CommandAllocator {
public:
  DirectX12CommandAllocator(ComPtr<ID3D12Device> device,
                            unsigned pipeline_depth);

  unsigned getPipelineDepth() const override;

  CommandBuffer *allocateCommandBuffer() override;

  void beginFrame() override;
  void endFrame() override;
};
} // namespace ren
