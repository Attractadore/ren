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
    unsigned depth = 1;
    unsigned array_layers;
  };
  unsigned mip_levels = 1;
};

struct TextureRef {
  TextureDesc desc;
  void *handle;
};

struct Texture {
  TextureDesc desc;
  AnyRef handle;

  operator TextureRef() const {
    return {
        .desc = desc,
        .handle = handle.get(),
    };
  }
};

constexpr auto PARENT_FORMAT = Format::Undefined;
constexpr unsigned ALL_MIP_LEVELS = -1;
constexpr unsigned ALL_ARRAY_LAYERS = -1;

struct RenderTargetViewDesc {
  Format format = PARENT_FORMAT;
  unsigned mip_level = 0;
  unsigned array_layer = 0;

  constexpr auto operator<=>(const RenderTargetViewDesc &) const = default;
};

struct RenderTargetView {
  RenderTargetViewDesc desc;
  TextureRef texture;

  static RenderTargetView create(const TextureRef &texture,
                                 RenderTargetViewDesc desc = {}) {
    if (desc.format == PARENT_FORMAT) {

      desc.format = texture.desc.format;
    }
    return {.desc = desc, .texture = texture};
  }
};

struct DepthStencilViewDesc {
  unsigned mip_level = 0;
  unsigned array_layer = 0;

  constexpr auto operator<=>(const DepthStencilViewDesc &) const = default;
};

struct DepthStencilView {
  DepthStencilViewDesc desc;
  TextureRef texture;

  static DepthStencilView create(const TextureRef &texture,
                                 DepthStencilViewDesc desc = {}) {
    return {.desc = desc, .texture = texture};
  }
};

#define REN_SAMPLED_TEXTURE_VIEW_TYPES                                         \
  (e1D)(Array1D)(e2D)(Array2D)(e3D)(Array3D)(Cube)(CubeArray)
REN_DEFINE_ENUM(SampledTextureViewType, REN_SAMPLED_TEXTURE_VIEW_TYPES);

#define REN_TEXTURE_CHANNELS (Identity)(R)(G)(B)(A)(One)(Zero)
REN_DEFINE_ENUM(TextureChannel, REN_TEXTURE_CHANNELS);

struct TextureComponentMapping {
  TextureChannel r, g, b, a;

  constexpr auto operator<=>(const TextureComponentMapping &) const = default;
};

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
  TextureRef texture;

  SampledTextureView create(const TextureRef &texture,
                            SampledTextureViewDesc desc = {}) {
    if (desc.format == PARENT_FORMAT) {
      desc.format = texture.desc.format;
    }
    if (desc.mip_levels == ALL_MIP_LEVELS) {
      desc.mip_levels = texture.desc.mip_levels - desc.first_mip_level;
    }
    if (desc.array_layers == ALL_ARRAY_LAYERS) {
      desc.array_layers = texture.desc.array_layers - desc.first_array_layer;
    }
    return {.desc = desc, .texture = texture};
  }
};

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
  TextureRef texture;

  static StorageTextureView create(const TextureRef &texture,
                                   StorageTextureViewDesc desc = {}) {
    if (desc.format == PARENT_FORMAT) {
      desc.format = texture.desc.format;
    }
    if (desc.array_layers == ALL_ARRAY_LAYERS) {
      desc.array_layers = texture.desc.array_layers - desc.first_array_layer;
    }
    return {.desc = desc, .texture = texture};
  }
};

} // namespace ren
