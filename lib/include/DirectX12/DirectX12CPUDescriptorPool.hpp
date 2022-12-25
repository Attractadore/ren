#pragma once
#include "DirectX12Descriptor.hpp"
#include "Support/ComPtr.hpp"
#include "Support/StackAllocatorPool.hpp"
#include "Support/Vector.hpp"

namespace ren {
class DirectX12CPUDescriptorPool {
  struct Heap {
    ComPtr<ID3D12DescriptorHeap> heap;
    D3D12_CPU_DESCRIPTOR_HANDLE start;
  };

  ID3D12Device *m_device;
  D3D12_DESCRIPTOR_HEAP_TYPE m_type;
  unsigned m_descriptor_size;
  SmallVector<Heap> m_heaps;
  StackAllocatorPool m_allocator_pool;

private:
  void create_heap();

public:
  DirectX12CPUDescriptorPool(ID3D12Device *device,
                             D3D12_DESCRIPTOR_HEAP_TYPE type,
                             unsigned heap_size = 1024);

  unsigned get_heap_size() const {
    return m_allocator_pool.get_allocator_capacity();
  }

  D3D12_CPU_DESCRIPTOR_HANDLE allocate();
  void free(D3D12_CPU_DESCRIPTOR_HANDLE descriptor);
};

} // namespace ren
