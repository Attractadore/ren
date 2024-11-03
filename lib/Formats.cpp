#include "Formats.hpp"

#include <tiny_imageformat/tinyimageformat.h>

namespace ren {

auto getVkImageAspectFlags(TinyImageFormat format) -> VkImageAspectFlags {
  if (TinyImageFormat_IsDepthAndStencil(format)) {
    return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
  } else if (TinyImageFormat_IsDepthOnly(format)) {
    return VK_IMAGE_ASPECT_DEPTH_BIT;
  } else if (TinyImageFormat_IsStencilOnly(format)) {
    return VK_IMAGE_ASPECT_STENCIL_BIT;
  }
  return VK_IMAGE_ASPECT_COLOR_BIT;
}

} // namespace ren
