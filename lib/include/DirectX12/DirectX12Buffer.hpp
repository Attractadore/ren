#pragma once
#include "Buffer.hpp"

#include <d3d12.h>

namespace ren {
inline D3D12_RESOURCE_FLAGS getD3D12ResourceFlags(BufferUsageFlags usage) {
  D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
  if (usage.isSet(BufferUsage::Storage)) {
    flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  }
  if (not usage.isSet(BufferUsage::Uniform)) {
    flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
  }
  return flags;
}

inline ID3D12Resource *getD3D12Resource(const BufferLike auto &buffer) {
  return reinterpret_cast<ID3D12Resource *>(buffer.get());
}

REN_MAP_TYPE(BufferLocation, D3D12_HEAP_TYPE);
REN_MAP_FIELD(BufferLocation::Device, D3D12_HEAP_TYPE_DEFAULT);
REN_MAP_FIELD(BufferLocation::Host, D3D12_HEAP_TYPE_UPLOAD);
REN_MAP_FIELD(BufferLocation::HostCached, D3D12_HEAP_TYPE_READBACK);
REN_MAP_ENUM(getD3D12HeapType, BufferLocation, REN_BUFFER_LOCATIONS);

} // namespace ren
