#pragma once
#include "Support/Enum.hpp"

namespace ren {
#define REN_FORMATS /*(RGB8)(RGB8_SRGB)(BGR8)(BGR8_SRGB)*/                     \
  (RGBA8)(RGBA8_SRGB)(BGRA8)(BGRA8_SRGB)(RGBA16F)

REN_DEFINE_ENUM_WITH_UNKNOWN(Format, REN_FORMATS);

#define REN_FORMAT_PROPERTIES (Color)(SRGB)(Depth)(Stencil)
REN_DEFINE_FLAGS_ENUM(FormatProperty, REN_FORMAT_PROPERTIES);

struct FormatProperties {
  FormatPropertyFlags flags;
};

#define REN_INDEX_FORMATS (U16)(U32)
REN_DEFINE_ENUM_WITH_UNKNOWN(IndexFormat, REN_INDEX_FORMATS);
} // namespace ren
