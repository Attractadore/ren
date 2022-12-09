#pragma once
#include "Formats.hpp"
#include "Support/Enum.hpp"
#include "Support/Ref.hpp"

namespace ren {
#define REN_TEXTURE_TYPES (e1D)(e2D)(e3D)
REN_DEFINE_ENUM_WITH_UNKNOWN(TextureType, REN_TEXTURE_TYPES);

#define REN_TEXTURE_USAGES                                                     \
  (RenderTarget)(DepthStencilTarget)(TransferSRC)(TransferDST)(Storage)(Sampled)
REN_DEFINE_FLAGS_ENUM(TextureUsage, REN_TEXTURE_USAGES);

struct TextureDesc {
  TextureType type = TextureType::e2D;
  Format format;
  TextureUsageFlags usage;
  unsigned width = 1;
  unsigned height = 1;
  union {
    unsigned short depth = 1;
    unsigned short layers;
  };
  unsigned short levels = 1;
};

struct Texture {
  TextureDesc desc;
  AnyRef handle;
};

struct RenderTargetViewDesc {
  Format format = Format::Undefined;
  unsigned level = 0;
  unsigned layer = 0;

  constexpr auto operator<=>(const RenderTargetViewDesc &) const = default;
};

struct RenderTargetView {
  RenderTargetViewDesc desc;
  Texture texture;
};

inline Format getRTVFormat(const RenderTargetView &rtv) {
  return rtv.desc.format != Format::Undefined ? rtv.desc.format
                                              : rtv.texture.desc.format;
}

struct DepthStencilViewDesc {
  unsigned level = 0;
  unsigned layer = 0;

  constexpr auto operator<=>(const DepthStencilViewDesc &) const = default;
};

struct DepthStencilView {
  DepthStencilViewDesc desc;
  Texture texture;
};

inline Format getDSVFormat(const DepthStencilView &dsv) {
  return dsv.texture.desc.format;
}
} // namespace ren
