#pragma once
#include "Formats.hpp"
#include "Support/Flags.hpp"
#include "Support/Ref.hpp"

namespace ren {
enum class TextureType {
  e1D,
  e2D,
  e3D,
};

// clang-format off
BEGIN_FLAGS_ENUM(TextureUsage) {
  FLAG(RenderTarget),
  FLAG(DepthStencilTarget),
  FLAG(TransferSRC),
  FLAG(TransferDST),
  FLAG(Storage),
  FLAG(Sampled),
} END_FLAGS_ENUM(TextureUsage);
// clang-format on

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

enum class TextureViewType {
  e2D,
};

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
