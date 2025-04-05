#pragma once
#include "core/StdDef.hpp"
#include "ren/tiny_imageformat.h"

namespace ren {

constexpr usize FORMAT_BITS = 8;
static_assert(TinyImageFormat_Count <= (1 << FORMAT_BITS));

constexpr TinyImageFormat HDR_FORMAT = TinyImageFormat_R16G16B16A16_SFLOAT;
constexpr TinyImageFormat SWAP_CHAIN_FORMAT = TinyImageFormat_B8G8R8A8_UNORM;
constexpr TinyImageFormat SDR_FORMAT = SWAP_CHAIN_FORMAT;
constexpr TinyImageFormat DEPTH_FORMAT = TinyImageFormat_D32_SFLOAT;

} // namespace ren
