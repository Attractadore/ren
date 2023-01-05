#pragma once
#include "CommandAllocator.hpp"
#include "Config.hpp"
#include "DirectX12CommandBuffer.hpp"
#include "DirectX12Descriptor.hpp"
#include "Support/ComPtr.hpp"
#include "Support/StableVector.hpp"

namespace ren {
class DirectX12CommandAllocator final : public CommandAllocator {
  DirectX12Device *m_device = nullptr;
  std::array<ComPtr<ID3D12CommandAllocator>, c_pipeline_depth>
      m_frame_cmd_allocators;
  StableVector<DirectX12CommandBuffer> m_cmd_buffers;
  unsigned m_used_cmd_buffer_count = 0;
  unsigned m_frame_index = 0;

  static constexpr unsigned c_descriptor_heap_size = 1024;
  unsigned m_allocated_descriptors = 0;
  unsigned m_descriptor_size = 0;
  ComPtr<ID3D12DescriptorHeap> m_descriptor_heap;

private:
  ID3D12CommandAllocator *getFrameCommandAllocator();

  DirectX12CommandBuffer *allocateDirectX12CommandBufferImpl();

public:
  DirectX12CommandAllocator(DirectX12Device &device);
  DirectX12CommandAllocator(const DirectX12CommandAllocator &) = delete;
  DirectX12CommandAllocator(DirectX12CommandAllocator &&);
  DirectX12CommandAllocator &
  operator=(const DirectX12CommandAllocator &) = delete;
  DirectX12CommandAllocator &operator=(DirectX12CommandAllocator &&);
  ~DirectX12CommandAllocator() = default;

  void begin_frame() override;
  void end_frame() override;

  DirectX12Device *getDevice() { return m_device; }

  DirectX12CommandBuffer *allocateDirectX12CommandBuffer();
  CommandBuffer *allocateCommandBuffer() override;
  Descriptor allocateDescriptors(unsigned count);
};
} // namespace ren
