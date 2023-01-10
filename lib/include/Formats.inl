#pragma once
#include "Formats.hpp"

namespace ren {

inline FormatProperties getFormatProperties(Format format) {
  using enum Format;
  using enum FormatProperty;
  switch (format) {
  default:
    assert(!"Unknown Format");
    return {};
  case RGBA8:
  case BGRA8: {
    return {
        .flags = Color,
        .red_bits = 8,
        .green_bits = 8,
        .blue_bits = 8,
        .alpha_bits = 8,
    };
  }
  case RGBA16F:
    return {
        .flags = Color,
        .red_bits = 16,
        .green_bits = 16,
        .blue_bits = 16,
        .alpha_bits = 16,
    };
  case RGB32F:
    return {
        .flags = Color,
        .red_bits = 32,
        .green_bits = 32,
        .blue_bits = 32,
    };
  case RGBA8_SRGB:
  case BGRA8_SRGB:
    return {
        .flags = Color | SRGB,
        .red_bits = 8,
        .green_bits = 8,
        .blue_bits = 8,
        .alpha_bits = 8,
    };
  }
}

inline bool isSRGBFormat(Format format) {
  return getFormatProperties(format).flags.isSet(FormatProperty::SRGB);
}

inline bool isColorFormat(Format format) {
  return getFormatProperties(format).flags.isSet(FormatProperty::Color);
}

inline bool isDepthFormat(Format format) {
  return getFormatProperties(format).flags.isSet(FormatProperty::Depth);
}

inline bool isStencilFormat(Format format) {
  return getFormatProperties(format).flags.isSet(FormatProperty::Stencil);
}

inline Format getSRGBFormat(Format format) {
  using enum Format;
  switch (format) {
  default:
    assert(!"Format without SRGB counterpart");
    return Undefined;
  case RGBA8:
    return RGBA8_SRGB;
  case BGRA8:
    return BGRA8_SRGB;
  }
}

inline unsigned get_format_size(Format format) {
  auto p = getFormatProperties(format);
  auto bits = [&] {
    if (isColorFormat(format)) {
      return p.red_bits + p.green_bits + p.blue_bits + p.alpha_bits;
    } else {
      return p.depth_bits + p.stencil_bits + p.unused_bits;
    }
  }();
  return bits / CHAR_BIT;
}

} // namespace ren
