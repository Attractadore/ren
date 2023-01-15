#pragma once
#include "Texture.hpp"

#include <d3d12.h>

namespace ren {

REN_MAP_TYPE(TextureType, D3D12_RESOURCE_DIMENSION);
REN_MAP_FIELD(TextureType::e1D, D3D12_RESOURCE_DIMENSION_TEXTURE1D);
REN_MAP_FIELD(TextureType::e2D, D3D12_RESOURCE_DIMENSION_TEXTURE2D);
REN_MAP_FIELD(TextureType::e3D, D3D12_RESOURCE_DIMENSION_TEXTURE3D);

REN_MAP_ENUM(getD3D12ResourceDimension, TextureType, REN_TEXTURE_TYPES);
REN_REVERSE_MAP_ENUM(getTextureType, TextureType, REN_TEXTURE_TYPES);

inline D3D12_RESOURCE_FLAGS getD3D12ResourceFlags(TextureUsageFlags usage) {
  D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
  if (usage.isSet(TextureUsage::RenderTarget)) {
    flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  }
  if (usage.isSet(TextureUsage::DepthStencilTarget)) {
    flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    if (!usage.isSet(TextureUsage::Sampled)) {
      flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
    }
  }
  if (usage.isSet(TextureUsage::Storage)) {
    flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  }
  return flags;
}

inline TextureUsageFlags getTextureUsageFlags(D3D12_RESOURCE_FLAGS flags) {
  auto usage = TextureUsage::TransferSRC | TextureUsage::TransferDST;
  if (flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) {
    usage |= TextureUsage::RenderTarget;
  }
  if (flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) {
    usage |= TextureUsage::DepthStencilTarget;
  }
  if (!(flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)) {
    usage |= TextureUsage::Sampled;
  }
  if (flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) {
    usage |= TextureUsage::Storage;
  }
  return usage;
}

inline ID3D12Resource *getD3D12Resource(const TextureRef &texture) {
  return reinterpret_cast<ID3D12Resource *>(texture.handle);
}

} // namespace ren
