#include "DirectX12/DirectX12CommandAllocator.hpp"
#include "DirectX12/DirectX12Device.hpp"
#include "DirectX12/Errors.hpp"

namespace ren {
DirectX12CommandAllocator::DirectX12CommandAllocator(DirectX12Device *device,
                                                     unsigned pipeline_depth) {
  m_device = device;
  for (int i = 0; i < pipeline_depth; ++i) {
    m_frame_cmd_allocators.emplace_back(
        m_device->createCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT));
  }
  m_frame_end_times.resize(pipeline_depth, 0);

  D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {
      .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
      .NumDescriptors = UINT(pipeline_depth * c_descriptor_heap_size),
      .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
  };

  throwIfFailed(m_device->get()->CreateDescriptorHeap(
                    &heap_desc, IID_PPV_ARGS(&m_descriptor_heap)),
                "D3D12: Failed to create shader-visible descriptor heap");
  m_descriptor_size = m_device->get()->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

DirectX12CommandAllocator::~DirectX12CommandAllocator() {
  m_device->waitForDirectQueueCompletion();
}

ID3D12CommandAllocator *DirectX12CommandAllocator::getFrameCommandAllocator() {
  return m_frame_cmd_allocators[m_frame_index].Get();
}

DirectX12CommandBuffer *
DirectX12CommandAllocator::allocateDirectX12CommandBufferImpl() {
  auto *cmd_alloc = getFrameCommandAllocator();
  if (m_used_cmd_buffer_count == m_cmd_buffers.size()) {
    m_cmd_buffers.emplace_back(m_device, this, cmd_alloc);
  } else {
    m_cmd_buffers[m_used_cmd_buffer_count].reset(cmd_alloc);
  }
  return &m_cmd_buffers[m_used_cmd_buffer_count++];
}

DirectX12CommandBuffer *
DirectX12CommandAllocator::allocateDirectX12CommandBuffer() {
  auto *dx_cmd = allocateDirectX12CommandBufferImpl();
  dx_cmd->get()->SetDescriptorHeaps(1, m_descriptor_heap.GetAddressOf());
  return dx_cmd;
}

CommandBuffer *DirectX12CommandAllocator::allocateCommandBuffer() {
  return allocateDirectX12CommandBuffer();
}

void DirectX12CommandAllocator::beginFrameImpl() {
  m_device->waitForDirectQueueCompletion(m_frame_end_times[m_frame_index]);
  throwIfFailed(getFrameCommandAllocator()->Reset(),
                "D3D12: Failed to reset command allocator");
  m_used_cmd_buffer_count = 0;
  m_allocated_descriptors = 0;
}

void DirectX12CommandAllocator::endFrameImpl() {
  m_frame_end_times[m_frame_index] = m_device->getDirectQueueTime();
  m_frame_index = (m_frame_index + 1) % getPipelineDepth();
}

Descriptor DirectX12CommandAllocator::allocateDescriptors(unsigned count) {
  size_t offset =
      (m_frame_index * c_descriptor_heap_size + m_allocated_descriptors) *
      m_descriptor_size;
  return {
      .cpu_handle =
          {m_descriptor_heap->GetCPUDescriptorHandleForHeapStart().ptr +
           offset},
      .gpu_handle =
          {m_descriptor_heap->GetGPUDescriptorHandleForHeapStart().ptr +
           offset},
  };
}
} // namespace ren
