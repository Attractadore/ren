#include "DirectX12/DirectX12CPUDescriptorPool.hpp"
#include "Support/Errors.hpp"

#include <range/v3/algorithm.hpp>

namespace ren {
DirectX12CPUDescriptorPool::DirectX12CPUDescriptorPool(
    ID3D12Device *device, D3D12_DESCRIPTOR_HEAP_TYPE type, unsigned heap_size)
    : m_allocator_pool(heap_size) {
  m_device = device;
  m_type = type;
  m_descriptor_size = m_device->GetDescriptorHandleIncrementSize(m_type);
}

void DirectX12CPUDescriptorPool::create_heap() {
  assert(m_device);
  auto &heap = m_heaps.emplace_back();
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {
      .Type = m_type,
      .NumDescriptors = get_heap_size(),
  };
  throwIfFailed(
      m_device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap.heap)),
      "D3D12: Failed to create descriptor heap");
  heap.start = heap.heap->GetCPUDescriptorHandleForHeapStart();
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectX12CPUDescriptorPool::allocate() {
  auto [allocation, start] = m_allocator_pool.allocate(1, 1);
  auto heap_idx = allocation.idx;
  if (heap_idx == m_heaps.size()) {
    create_heap();
  }
  assert(heap_idx < m_heaps.size());
  return {m_heaps[heap_idx].start.ptr + start * m_descriptor_size};
}

void DirectX12CPUDescriptorPool::free(D3D12_CPU_DESCRIPTOR_HANDLE descriptor) {
  auto heap_size = get_heap_size();
  unsigned heap_idx = ranges::distance(
      m_heaps.begin(), ranges::find_if(m_heaps, [&](const Heap &heap) {
        return heap.start.ptr <= descriptor.ptr and
               descriptor.ptr < heap.start.ptr + heap_size * m_descriptor_size;
      }));
  assert(heap_idx < m_heaps.size());
  m_allocator_pool.free(
      StackAllocatorPool::Allocation{.idx = heap_idx, .count = 1});
}
} // namespace ren
