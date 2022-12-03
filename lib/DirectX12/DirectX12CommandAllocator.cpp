#include "DirectX12/DirectX12CommandAllocator.hpp"
#include "DirectX12/Errors.hpp"

namespace ren {
DirectX12CommandAllocator::DirectX12CommandAllocator(ID3D12Device *device,
                                                     ID3D12CommandQueue *queue,
                                                     uint64_t pipeline_depth)
    : CommandAllocator(pipeline_depth) {
  m_device = device;
  m_queue = queue;

  for (int i = 0; i < pipeline_depth; ++i) {
    m_frame_cmd_allocators.emplace_back();
    throwIfFailed(m_device->CreateCommandAllocator(
                      D3D12_COMMAND_LIST_TYPE_DIRECT,
                      IID_PPV_ARGS(&m_frame_cmd_allocators.back())),
                  "D3D12: Failed to create command allocator");
  }

  throwIfFailed(m_device->CreateFence(getFrameNumber(), D3D12_FENCE_FLAG_NONE,
                                      IID_PPV_ARGS(&m_fence)),
                "D3D12: Failed to create fence");
  m_event = CreateEvent(nullptr, false, false, nullptr);
  throwIfFailed(m_event, "WIN32: Failed to create event handle");
}

DirectX12CommandAllocator::~DirectX12CommandAllocator() {
  waitForFrame(getFrameNumber());
  CloseHandle(m_event);
}

ID3D12CommandAllocator *DirectX12CommandAllocator::getFrameCommandAllocator() {
  return m_frame_cmd_allocators[getFrameIndex()].Get();
}

DirectX12CommandBuffer *
DirectX12CommandAllocator::allocateDirectX12CommandBuffer() {
  auto cmd_alloc = getFrameCommandAllocator();
  if (m_used_cmd_buffer_count == m_cmd_buffers.size()) {
    m_cmd_buffers.emplace_back(this, m_device, cmd_alloc);
  } else {
    m_cmd_buffers.back().reset(cmd_alloc);
  }
  ++m_used_cmd_buffer_count;
  return &m_cmd_buffers.back();
}

CommandBuffer *DirectX12CommandAllocator::allocateCommandBuffer() {
  return allocateDirectX12CommandBuffer();
}

void DirectX12CommandAllocator::waitForFrame(uint64_t frame) {
  if (m_fence->GetCompletedValue() < frame) {
    throwIfFailed(m_fence->SetEventOnCompletion(frame, m_event),
                  "D3D12: Failed to set fence completion event");
    throwIfFailed(WaitForSingleObject(m_event, INFINITE),
                  "WIN32: Failed to wait for event");
  }
}

void DirectX12CommandAllocator::beginFrameImpl() {
  throwIfFailed(getFrameCommandAllocator()->Reset(),
                "D3D12: Failed to reset command allocator");
  m_used_cmd_buffer_count = 0;
}

void DirectX12CommandAllocator::endFrameImpl() {
  m_queue->Signal(m_fence.Get(), getFrameNumber());
}
} // namespace ren
