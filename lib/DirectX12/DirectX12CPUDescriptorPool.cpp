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
  auto &heap = m_heaps.emplace_back();
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {
      .Type = m_type,
      .NumDescriptors = m_heap_size,
  };
  throwIfFailed(
      m_device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap.heap)),
      "D3D12: Failed to create descriptor heap");
  heap.cpu_handle = heap.heap->GetCPUDescriptorHandleForHeapStart();
  heap.gpu_handle = heap.heap->GetGPUDescriptorHandleForHeapStart();
}

unsigned DirectX12CPUDescriptorPool::findFreeHeap() const {
  return ranges::distance(m_heaps.begin(),
                          ranges::find_if(m_heaps, [&](const Heap &heap) {
                            return heap.num_allocated < m_heap_size;
                          }));
}

auto DirectX12CPUDescriptorPool::allocate() -> Descriptor {
  auto heap_index = findFreeHeap();
  if (heap_index == m_heaps.size()) {
    createHeap();
  }
  auto &heap = m_heaps[heap_index];
  auto offset = heap.num_allocated++ * m_descriptor_size;
  return {
      .cpu_handle = {heap.cpu_handle.ptr + offset},
      .gpu_handle = {heap.gpu_handle.ptr + offset},
  };
}

void DirectX12CPUDescriptorPool::free(Descriptor descriptor) {
  auto &heap = *ranges::find_if(m_heaps, [&](const Heap &heap) {
    return heap.cpu_handle.ptr <= descriptor.cpu_handle.ptr and
           descriptor.cpu_handle.ptr <
               heap.cpu_handle.ptr + m_heap_size * m_descriptor_size;
  });
  if (++heap.num_freed == heap.num_allocated) {
    heap.num_allocated = 0;
    heap.num_freed = 0;
  }
}
} // namespace ren
