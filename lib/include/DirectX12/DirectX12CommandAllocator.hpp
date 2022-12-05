#pragma once
#include "CommandAllocator.hpp"
#include "DirectX12CommandBuffer.hpp"
#include "Support/ComPtr.hpp"
#include "Support/StableVector.hpp"

namespace ren {
class DirectX12CommandAllocator final : public CommandAllocator {
  ID3D12Device *m_device;
  ID3D12CommandQueue *m_queue;
  ComPtr<ID3D12Fence> m_fence;
  HANDLE m_event;
  SmallVector<ComPtr<ID3D12CommandAllocator>, 3> m_frame_cmd_allocators;
  StableVector<DirectX12CommandBuffer> m_cmd_buffers;
  unsigned m_used_cmd_buffer_count;

private:
  ID3D12CommandAllocator *getFrameCommandAllocator();

  void waitForFrame(uint64_t frame) override;
  void beginFrameImpl() override;
  void endFrameImpl() override;

public:
  DirectX12CommandAllocator(ID3D12Device *device, ID3D12CommandQueue *queue,
                            uint64_t pipeline_depth);
  ~DirectX12CommandAllocator();

  DirectX12CommandBuffer *allocateDirectX12CommandBuffer();
  CommandBuffer *allocateCommandBuffer() override;

  ID3D12CommandQueue* getQueue() { return m_queue; }

  void flush();
};
} // namespace ren
