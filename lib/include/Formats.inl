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
#if 0
  case RGB8:
  case BGR8:
#endif
  case RGBA8:
  case BGRA8:
  case RGBA16F:
    return {.flags = Color};
#if 0
  case RGB8_SRGB:
  case BGR8_SRGB:
#endif
  case RGBA8_SRGB:
  case BGRA8_SRGB:
    return {.flags = Color | SRGB};
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
} // namespace ren
