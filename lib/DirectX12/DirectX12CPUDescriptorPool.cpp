#include "DirectX12/DirectX12CPUDescriptorPool.hpp"
#include "Support/Errors.hpp"

#include <range/v3/algorithm.hpp>

namespace ren {
DirectX12CPUDescriptorPool::DirectX12CPUDescriptorPool(
    ID3D12Device *device, D3D12_DESCRIPTOR_HEAP_TYPE type, unsigned heap_size) {

  m_device = device;
  m_type = type;
  m_descriptor_size = m_device->GetDescriptorHandleIncrementSize(m_type);
  m_heap_size = heap_size;
}

void DirectX12CPUDescriptorPool::createHeap() {
  assert(m_device);
  auto &heap = m_heaps.emplace_back();
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {
      .Type = m_type,
      .NumDescriptors = m_heap_size,
  };
  throwIfFailed(
      m_device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap.heap)),
      "D3D12: Failed to create descriptor heap");
  heap.start = heap.heap->GetCPUDescriptorHandleForHeapStart();
}

unsigned DirectX12CPUDescriptorPool::findFreeHeap() const {
  return ranges::distance(m_heaps.begin(),
                          ranges::find_if(m_heaps, [&](const Heap &heap) {
                            return heap.num_allocated < m_heap_size;
                          }));
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectX12CPUDescriptorPool::allocate() {
  auto heap_index = findFreeHeap();
  if (heap_index == m_heaps.size()) {
    createHeap();
  }
  auto &heap = m_heaps[heap_index];
  auto offset = heap.num_allocated++ * m_descriptor_size;
  return {heap.start.ptr + offset};
}

void DirectX12CPUDescriptorPool::free(D3D12_CPU_DESCRIPTOR_HANDLE descriptor) {
  auto &heap = *ranges::find_if(m_heaps, [&](const Heap &heap) {
    return heap.start.ptr <= descriptor.ptr and
           descriptor.ptr < heap.start.ptr + m_heap_size * m_descriptor_size;
  });
  if (++heap.num_freed == heap.num_allocated) {
    heap.num_allocated = 0;
    heap.num_freed = 0;
  }
}
} // namespace ren
