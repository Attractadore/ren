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

enum class TextureUsage {
  RenderTarget = 1 << 0,
  TransferSRC = 1 << 1,
  TransferDST = 1 << 2,
  Storage = 1 << 3,
};

template <> struct EnableFlags<TextureUsage> {
  static constexpr bool enable = true;
};

using TextureUsageFlags = Flags<TextureUsage>;

struct TextureDesc {
  TextureType type = TextureType::e2D;
  Format format;
  TextureUsageFlags usage;
  unsigned width = 1;
  unsigned height = 1;
  unsigned layers = 1;
  union {
    unsigned depth = 1;
    unsigned levels;
  };
};

struct Texture {
  TextureDesc desc;
  Ref<void> handle;
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
