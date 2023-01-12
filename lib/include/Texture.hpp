#pragma once
#include "Formats.hpp"
#include "Support/Enum.hpp"
#include "Support/Ref.hpp"

namespace ren {
#define REN_TEXTURE_TYPES (e1D)(e2D)(e3D)
REN_DEFINE_ENUM_WITH_UNKNOWN(TextureType, REN_TEXTURE_TYPES);

#define REN_TEXTURE_USAGES                                                     \
  (TransferSRC)            /**/                                                \
      (TransferDST)        /**/                                                \
      (RenderTarget)       /**/                                                \
      (DepthStencilTarget) /**/                                                \
      (Sampled)            /* Texture */                                       \
      (Storage)            /* RWTexture */
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

constexpr auto PARENT_FORMAT = Format::Undefined;

struct RenderTargetViewDesc {
  Format format = PARENT_FORMAT;
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

#define REN_SAMPLED_TEXTURE_VIEW_TYPES                                         \
  (e1D)(Array1D)(e2D)(Array2D)(e3D)(Array3D)(Cube)(CubeArray)
REN_DEFINE_ENUM(SampledTextureViewType, REN_SAMPLED_TEXTURE_VIEW_TYPES);

#define REN_TEXTURE_CHANNELS (Identity)(R)(G)(B)(A)(One)(Zero)
REN_DEFINE_ENUM(TextureChannel, REN_TEXTURE_CHANNELS);

struct TextureComponentMapping {
  TextureChannel r, g, b, a;

  constexpr auto operator<=>(const TextureComponentMapping &) const = default;
};

constexpr unsigned ALL_MIP_LEVELS = -1;
constexpr unsigned ALL_ARRAY_LAYERS = -1;

struct SampledTextureViewDesc {
  SampledTextureViewType type = SampledTextureViewType::e2D;
  Format format = PARENT_FORMAT;
  TextureComponentMapping components;
  unsigned first_mip_level = 0;
  unsigned mip_levels = ALL_MIP_LEVELS;
  unsigned first_array_layer = 0;
  unsigned array_layers = 1;

  constexpr auto operator<=>(const SampledTextureViewDesc &) const = default;
};

struct SampledTextureView {
  SampledTextureViewDesc desc;
  Texture texture;
};

inline Format getSampledViewFormat(const SampledTextureView &view) {
  return view.desc.format == PARENT_FORMAT ? view.texture.desc.format
                                           : view.desc.format;
}

inline unsigned getSampledViewMipLevels(const SampledTextureView &view) {
  return view.desc.mip_levels == ALL_MIP_LEVELS
             ? view.texture.desc.levels - view.desc.first_mip_level
             : view.desc.mip_levels;
}

inline unsigned getSampledViewArrayLayers(const SampledTextureView &view) {
  return view.desc.array_layers == ALL_ARRAY_LAYERS
             ? view.texture.desc.layers - view.desc.first_array_layer
             : view.desc.array_layers;
}

#define REN_STORAGE_TEXTURE_VIEW_TYPES                                         \
  (e1D)(Array1D)(e2D)(Array2D)(e3D)(Array3D)
REN_DEFINE_ENUM(StorageTextureViewType, REN_STORAGE_TEXTURE_VIEW_TYPES);

struct StorageTextureViewDesc {
  StorageTextureViewType type = StorageTextureViewType::e2D;
  Format format = PARENT_FORMAT;
  unsigned mip_level = 0;
  unsigned first_array_layer = 0;
  unsigned array_layers = 1;

  constexpr auto operator<=>(const StorageTextureViewDesc &) const = default;
};

struct StorageTextureView {
  StorageTextureViewDesc desc;
  Texture texture;
};

inline Format getStorageViewFormat(const StorageTextureView &stv) {
  return stv.desc.format == PARENT_FORMAT ? stv.texture.desc.format
                                          : stv.desc.format;
}

inline unsigned getStorageViewArrayLayers(const StorageTextureView &view) {
  return view.desc.array_layers == ALL_ARRAY_LAYERS
             ? view.texture.desc.layers - view.desc.first_array_layer
             : view.desc.array_layers;
}
} // namespace ren
