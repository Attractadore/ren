#pragma once
#include "Support/Enum.hpp"

namespace ren {

#define REN_FORMATS (RGBA8)(RGBA8_SRGB)(BGRA8)(BGRA8_SRGB)(RGBA16F)(RGB32F)

REN_DEFINE_ENUM_WITH_UNKNOWN(Format, REN_FORMATS);

#define REN_FORMAT_PROPERTIES (Color)(SRGB)(Depth)(Stencil)
REN_DEFINE_FLAGS_ENUM(FormatProperty, REN_FORMAT_PROPERTIES);

struct FormatProperties {
  FormatPropertyFlags flags;
  union {
    struct {
      unsigned red_bits = 0;
      unsigned green_bits = 0;
      unsigned blue_bits = 0;
      unsigned alpha_bits = 0;
    };
    struct {
      unsigned depth_bits;
      unsigned stencil_bits;
      unsigned unused_bits;
    };
  };
};

#define REN_INDEX_FORMATS (U16)(U32)
REN_DEFINE_ENUM(IndexFormat, REN_INDEX_FORMATS);

} // namespace ren
