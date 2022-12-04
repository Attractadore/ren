#pragma once
#include "Formats.inl"
#include "Support/Enum.hpp"

#include <dxgiformat.h>

namespace ren {
namespace detail {
constexpr std::array format_map = {
    std::pair(Format::RGBA8, DXGI_FORMAT_R8G8B8A8_UNORM),
    std::pair(Format::RGBA8_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB),
    std::pair(Format::BGRA8, DXGI_FORMAT_B8G8R8A8_UNORM),
    std::pair(Format::BGRA8_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB),
    std::pair(Format::RGBA16F, DXGI_FORMAT_R16G16B16A16_FLOAT),
};
}

constexpr auto getDXGIFormat = enumMap<detail::format_map>;
constexpr auto getFormat = inverseEnumMap<detail::format_map>;
} // namespace ren
