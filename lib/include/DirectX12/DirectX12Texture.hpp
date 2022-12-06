#pragma once
#include "Support/Enum.hpp"
#include "Texture.hpp"

#include <d3d12.h>

namespace ren {
namespace detail {
constexpr std::array resource_dimension_map = {
    std::pair(TextureType::e1D, D3D12_RESOURCE_DIMENSION_TEXTURE1D),
    std::pair(TextureType::e2D, D3D12_RESOURCE_DIMENSION_TEXTURE2D),
    std::pair(TextureType::e3D, D3D12_RESOURCE_DIMENSION_TEXTURE3D),
};

template <> struct FlagsTypeImpl<D3D12_RESOURCE_FLAGS> {
  using type = D3D12_RESOURCE_FLAGS;
};

constexpr std::array resource_flag_map = {
    std::pair(TextureUsage::RenderTarget,
              D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
    std::pair(TextureUsage::DepthStencilTarget,
              D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
    std::pair(TextureUsage::Storage,
              D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
};

constexpr auto getD3D12ResourceFlagsHelper =
    flagsMap<resource_flag_map>;
constexpr auto getTextureUsageHelper =
    inverseFlagsMap<resource_flag_map>;
} // namespace detail

constexpr auto getD3D12ResourceDimension =
    enumMap<detail::resource_dimension_map>;
constexpr auto getTextureType = inverseEnumMap<detail::resource_dimension_map>;

inline D3D12_RESOURCE_FLAGS getD3D12ResourceFlags(TextureUsageFlags usage) {
  auto flags = detail::getD3D12ResourceFlagsHelper(usage);
  if (!usage.isSet(TextureUsage::Sampled)) {
    flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
  }
  return flags;
}

inline TextureUsageFlags getTextureUsageFlags(D3D12_RESOURCE_FLAGS flags) {
  auto usage = detail::getTextureUsageHelper(flags);
  if (!(flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)) {
    usage |= TextureUsage::Sampled;
  }
  return usage;
}

inline ID3D12Resource *getD3D12Resource(const Texture &tex) {
  return reinterpret_cast<ID3D12Resource *>(tex.handle.get());
}
} // namespace ren
