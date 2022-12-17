#pragma once
#include "CommandAllocator.hpp"
#include "DeviceHandle.hpp"
#include "DirectX12CommandBuffer.hpp"
#include "DirectX12Descriptor.hpp"
#include "Support/ComPtr.hpp"
#include "Support/StableVector.hpp"

namespace ren {
class DirectX12CommandAllocator final : public CommandAllocator {
  DirectX12Device *m_device;
  SmallVector<DeviceHandle<ID3D12CommandAllocator>, 3> m_frame_cmd_allocators;
  SmallVector<uint64_t, 3> m_frame_end_times;
  StableVector<DirectX12CommandBuffer> m_cmd_buffers;
  unsigned m_used_cmd_buffer_count;
  unsigned m_frame_index = 0;

  static constexpr unsigned c_descriptor_heap_size = 1024;
  unsigned m_allocated_descriptors;
  unsigned m_descriptor_size;
  DeviceHandle<ID3D12DescriptorHeap> m_descriptor_heap;

private:
  ID3D12CommandAllocator *getFrameCommandAllocator();

  void beginFrameImpl() override;
  void endFrameImpl() override;

  DirectX12CommandBuffer *allocateDirectX12CommandBufferImpl();

public:
  DirectX12CommandAllocator(DirectX12Device *device, unsigned pipeline_depth);
  DirectX12CommandAllocator(const DirectX12CommandAllocator &) = delete;
  DirectX12CommandAllocator(DirectX12CommandAllocator &&);
  DirectX12CommandAllocator &
  operator=(const DirectX12CommandAllocator &) = delete;
  DirectX12CommandAllocator &operator=(DirectX12CommandAllocator &&);
  ~DirectX12CommandAllocator();

  DirectX12Device *getDevice() { return m_device; }

  unsigned getPipelineDepth() const override {
    return m_frame_cmd_allocators.size();
  };

  DirectX12CommandBuffer *allocateDirectX12CommandBuffer();
  CommandBuffer *allocateCommandBuffer() override;
  Descriptor allocateDescriptors(unsigned count);
};
} // namespace ren
