#pragma once
#include "Support/Enum.hpp"
#include "ren/ren.h"

namespace ren {

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
    };
    unsigned unused_bits;
  };
};

auto getVkFormat(RenFormat format) -> VkFormat;

} // namespace ren
