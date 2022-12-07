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

struct TextureSubresource {
  unsigned first_mip_level = 0;
  unsigned mip_level_count = 1;
  unsigned first_layer = 0;
  unsigned layer_count = 1;

  auto operator<=>(const TextureSubresource &) const = default;
};

#define REN_TEXTURE_VIEW_TYPES (e2D)
REN_DEFINE_ENUM_WITH_UNKNOWN(TextureViewType, REN_TEXTURE_VIEW_TYPES);

struct TextureViewDesc {
  TextureViewType type = TextureViewType::e2D;
  TextureSubresource subresource;

  auto operator<=>(const TextureViewDesc &) const = default;
};

struct TextureView {
  TextureViewDesc desc;
  Texture texture;
};
} // namespace ren
