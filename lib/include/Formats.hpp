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

// clang-format off
BEGIN_FLAGS_ENUM(FormatProperty) {
  FLAG(Color),
  FLAG(SRGB),
  FLAG(Depth),
  FLAG(Stencil),
} END_FLAGS_ENUM(FormatProperty);
// clang-format on

struct FormatProperties {
  FormatPropertyFlags flags;
};
}
