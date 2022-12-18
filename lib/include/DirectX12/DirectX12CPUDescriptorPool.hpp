#pragma once
#include "DirectX12Descriptor.hpp"
#include "Support/ComPtr.hpp"
#include "Support/Vector.hpp"

namespace ren {
class DirectX12CPUDescriptorPool {
  struct Heap {
    ComPtr<ID3D12DescriptorHeap> heap;
    unsigned num_allocated = 0;
    unsigned num_freed = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE start;
  };

  ID3D12Device *m_device = nullptr;
  D3D12_DESCRIPTOR_HEAP_TYPE m_type;
  unsigned m_descriptor_size;
  unsigned m_heap_size;
  SmallVector<Heap> m_heaps;

private:
  void createHeap();
  unsigned findFreeHeap() const;

public:
  DirectX12CPUDescriptorPool() = default;
  DirectX12CPUDescriptorPool(ID3D12Device *device,
                             D3D12_DESCRIPTOR_HEAP_TYPE type,
                             unsigned heap_size = 1024);

  D3D12_CPU_DESCRIPTOR_HANDLE allocate();
  void free(D3D12_CPU_DESCRIPTOR_HANDLE descriptor);
};

} // namespace ren
