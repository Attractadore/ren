#pragma once
#include "Support/Flags.hpp"

namespace ren {
enum class Format {
  Undefined = 0,
  RGB8,
  RGB8_SRGB,
  BGR8,
  BGR8_SRGB,
  RGBA8,
  RGBA8_SRGB,
  BGRA8,
  BGRA8_SRGB,
  RGBA16F,
};

enum class FormatProperty {
  Color = 1 << 0,
  SRGB = 1 << 1,
  Depth = 1 << 2,
  Stencil = 1 << 3,
};

ENABLE_FLAGS(FormatProperty);

struct FormatProperties {
  FormatPropertyFlags flags;
};
}
