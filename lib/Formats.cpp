#include "Formats.hpp"
#include "Errors.hpp"

namespace ren {
auto getVkFormat(RenFormat format) -> VkFormat {
  switch (format) {
  case REN_FORMAT_R8_UNORM:
    return VK_FORMAT_R8_UNORM;
  case REN_FORMAT_R8_SRGB:
    return VK_FORMAT_R8_SRGB;
  case REN_FORMAT_RG8_UNORM:
    return VK_FORMAT_R8G8_UNORM;
  case REN_FORMAT_RG8_SRGB:
    return VK_FORMAT_R8G8_SRGB;
  case REN_FORMAT_RGB8_UNORM:
    return VK_FORMAT_R8G8B8_UNORM;
  case REN_FORMAT_RGB8_SRGB:
    return VK_FORMAT_R8G8B8_SRGB;
  case REN_FORMAT_RGBA8_UNORM:
    return VK_FORMAT_R8G8B8A8_UNORM;
  case REN_FORMAT_RGBA8_SRGB:
    return VK_FORMAT_R8G8B8A8_SRGB;
  case REN_FORMAT_R16_UNORM:
    return VK_FORMAT_R16_UNORM;
  case REN_FORMAT_RG16_UNORM:
    return VK_FORMAT_R16G16_UNORM;
  case REN_FORMAT_RGB16_UNORM:
    return VK_FORMAT_R16G16B16_UNORM;
  case REN_FORMAT_RGBA16_UNORM:
    return VK_FORMAT_R16G16B16A16_UNORM;
  case REN_FORMAT_RGB32_SFLOAT:
    return VK_FORMAT_R32G32B32_SFLOAT;
  case REN_FORMAT_RGBA32_SFLOAT:
    return VK_FORMAT_R32G32B32_SFLOAT;
  }
  unreachable("Unknown format {}", int(format));
}
} // namespace ren
