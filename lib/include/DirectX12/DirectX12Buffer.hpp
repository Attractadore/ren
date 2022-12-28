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
} // namespace ren
