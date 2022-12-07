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

#define REN_D3D12_TEXTURE_USAGES (RenderTarget)(DepthStencilTarget)(Storage)
REN_MAP_TYPE(TextureUsage, D3D12_RESOURCE_FLAGS);
REN_MAP_FIELD(TextureUsage::RenderTarget,
              D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
REN_MAP_FIELD(TextureUsage::DepthStencilTarget,
              D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
REN_MAP_FIELD(TextureUsage::Storage,
              D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

namespace detail {
REN_MAP_ENUM(getD3D12ResourceUsage, TextureUsage, REN_D3D12_TEXTURE_USAGES);
REN_REVERSE_MAP_ENUM(getTextureUsage, TextureUsage, REN_D3D12_TEXTURE_USAGES);
} // namespace detail

#undef REN_D3D12_TEXTURE_USAGES

inline D3D12_RESOURCE_FLAGS getD3D12ResourceFlag(TextureUsage usage) {
  switch (usage) {
  case TextureUsage::TransferSRC:
  case TextureUsage::TransferDST:
  case TextureUsage::Sampled:
    return {};
  default:
    return detail::getD3D12ResourceUsage(usage);
  }
}

inline D3D12_RESOURCE_FLAGS getD3D12ResourceFlags(TextureUsageFlags usage) {
  auto flags = detail::mapFlags<TextureUsage, getD3D12ResourceFlag>(usage);
  if (!usage.isSet(TextureUsage::Sampled)) {
    flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
  }
  return flags;
}

inline TextureUsageFlags getTextureUsageFlags(D3D12_RESOURCE_FLAGS flags) {
  auto usage =
      detail::reverseMapFlags<TextureUsage, detail::getTextureUsage>(flags);
  if (!(flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)) {
    usage |= TextureUsage::Sampled;
  }
  usage |= TextureUsage::TransferSRC;
  usage |= TextureUsage::TransferDST;
  return usage;
}

inline ID3D12Resource *getD3D12Resource(const Texture &tex) {
  return reinterpret_cast<ID3D12Resource *>(tex.handle.get());
}
} // namespace ren
