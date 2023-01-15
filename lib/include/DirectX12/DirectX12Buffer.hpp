#pragma once
#include "Buffer.hpp"

#include <d3d12.h>

namespace ren {

REN_MAP_TYPE(BufferHeap, D3D12_HEAP_TYPE);
REN_MAP_FIELD(BufferHeap::Device, D3D12_HEAP_TYPE_DEFAULT);
REN_MAP_FIELD(BufferHeap::Upload, D3D12_HEAP_TYPE_UPLOAD);
REN_MAP_FIELD(BufferHeap::Readback, D3D12_HEAP_TYPE_READBACK);
REN_MAP_ENUM(getD3D12HeapType, BufferHeap, REN_BUFFER_HEAPS);

inline D3D12_RESOURCE_FLAGS getD3D12ResourceFlags(BufferUsageFlags usage) {
  D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
  if (usage.anySet(BufferUsage::RWTexel | BufferUsage::RWStorage)) {
    flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  }
  return flags;
}

inline ID3D12Resource *getD3D12Resource(const BufferRef &buffer) {
  return reinterpret_cast<ID3D12Resource *>(buffer.handle);
}

} // namespace ren
