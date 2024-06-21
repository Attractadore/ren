#include "Formats.hpp"
#include "Support/Assert.hpp"
#include "Support/Errors.hpp"

namespace ren {

auto getVkFormat(Format format) -> VkFormat {
  using enum Format;
  switch (format) {
  default:
    unreachable("Unknown format {}", int(format));
  case R8_UNORM:
    return VK_FORMAT_R8_UNORM;
  case R8_SRGB:
    return VK_FORMAT_R8_SRGB;
  case RG8_UNORM:
    return VK_FORMAT_R8G8_UNORM;
  case RG8_SRGB:
    return VK_FORMAT_R8G8_SRGB;
  case RGB8_UNORM:
    return VK_FORMAT_R8G8B8_UNORM;
  case RGB8_SRGB:
    return VK_FORMAT_R8G8B8_SRGB;
  case RGBA8_UNORM:
    return VK_FORMAT_R8G8B8A8_UNORM;
  case RGBA8_SRGB:
    return VK_FORMAT_R8G8B8A8_SRGB;
  case BGRA8_UNORM:
    return VK_FORMAT_B8G8R8A8_UNORM;
  case BGRA8_SRGB:
    return VK_FORMAT_B8G8R8A8_SRGB;
  case R16_UNORM:
    return VK_FORMAT_R16_UNORM;
  case RG16_UNORM:
    return VK_FORMAT_R16G16_UNORM;
  case RGB16_UNORM:
    return VK_FORMAT_R16G16B16_UNORM;
  case RGBA16_UNORM:
    return VK_FORMAT_R16G16B16A16_UNORM;
  case RGB32_SFLOAT:
    return VK_FORMAT_R32G32B32_SFLOAT;
  case RGBA32_SFLOAT:
    return VK_FORMAT_R32G32B32_SFLOAT;
  }
}

FormatProperties getFormatProperties(VkFormat format) {
  using enum FormatProperty;
  switch (format) {
  default:
    unreachable("Unknown VkFormat {}", int(format));
  case VK_FORMAT_R8G8B8_UNORM: {
    return {
        .flags = Color,
        .red_bits = 8,
        .green_bits = 8,
        .blue_bits = 8,
    };
  }
  case VK_FORMAT_R8G8B8_SRGB: {
    return {
        .flags = Color | SRGB,
        .red_bits = 8,
        .green_bits = 8,
        .blue_bits = 8,
    };
  }
  case VK_FORMAT_R8G8B8A8_UNORM:
  case VK_FORMAT_B8G8R8A8_UNORM: {
    return {
        .flags = Color,
        .red_bits = 8,
        .green_bits = 8,
        .blue_bits = 8,
        .alpha_bits = 8,
    };
  }
  case VK_FORMAT_R8G8B8A8_SRGB:
  case VK_FORMAT_B8G8R8A8_SRGB:
    return {
        .flags = Color | SRGB,
        .red_bits = 8,
        .green_bits = 8,
        .blue_bits = 8,
        .alpha_bits = 8,
    };
  case VK_FORMAT_B10G11R11_UFLOAT_PACK32: {
    return {
        .flags = Color,
        .red_bits = 11,
        .green_bits = 11,
        .blue_bits = 10,
    };
  }
  case VK_FORMAT_R16_SFLOAT:
    return {
        .flags = Color,
        .red_bits = 16,
    };
  case VK_FORMAT_R16G16_SFLOAT:
    return {
        .flags = Color,
        .red_bits = 16,
        .green_bits = 16,
    };
  case VK_FORMAT_R16G16B16_SFLOAT:
    return {
        .flags = Color,
        .red_bits = 16,
        .green_bits = 16,
        .blue_bits = 16,
    };
  case VK_FORMAT_R16G16B16A16_SFLOAT:
    return {
        .flags = Color,
        .red_bits = 16,
        .green_bits = 16,
        .blue_bits = 16,
        .alpha_bits = 16,
    };
  case VK_FORMAT_R32_SFLOAT:
    return {
        .flags = Color,
        .red_bits = 32,
    };
  case VK_FORMAT_R32G32_SFLOAT:
    return {
        .flags = Color,
        .red_bits = 32,
        .green_bits = 32,
    };
  case VK_FORMAT_R32G32B32_SFLOAT:
    return {
        .flags = Color,
        .red_bits = 32,
        .green_bits = 32,
        .blue_bits = 32,
    };
  case VK_FORMAT_R32G32B32A32_SFLOAT:
    return {
        .flags = Color,
        .red_bits = 32,
        .green_bits = 32,
        .blue_bits = 32,
        .alpha_bits = 32,
    };
  case VK_FORMAT_D32_SFLOAT: {
    return {
        .flags = Depth,
        .depth_bits = 32,
    };
  }
  }
}

bool isSRGBFormat(VkFormat format) {
  return getFormatProperties(format).flags.is_set(FormatProperty::SRGB);
}

bool isColorFormat(VkFormat format) {
  return getFormatProperties(format).flags.is_set(FormatProperty::Color);
}

bool isDepthFormat(VkFormat format) {
  return getFormatProperties(format).flags.is_set(FormatProperty::Depth);
}

bool isStencilFormat(VkFormat format) {
  return getFormatProperties(format).flags.is_set(FormatProperty::Stencil);
}

VkFormat getSRGBFormat(VkFormat format) {
  using enum VkFormat;
  switch (format) {
  default: {
    ren_assert(
        not getFormatProperties(format).flags.is_set(FormatProperty::SRGB));
    unreachable("VkFormat {} doesn't have an SRGB counterpart", int(format));
  }
  case VK_FORMAT_R8G8B8A8_UNORM:
    return VK_FORMAT_R8G8B8A8_SRGB;
  case VK_FORMAT_B8G8R8A8_UNORM:
    return VK_FORMAT_B8G8R8A8_SRGB;
  }
}

auto get_format_size(VkFormat format) -> u32 {
  auto p = getFormatProperties(format);
  auto bits = [&] {
    if (isColorFormat(format)) {
      return p.red_bits + p.green_bits + p.blue_bits + p.alpha_bits;
    } else {
      return p.depth_bits + p.stencil_bits + p.unused_bits;
    }
  }();
  return bits / 8;
}

VkImageAspectFlags getVkImageAspectFlags(VkFormat format) {
  if (isColorFormat(format)) {
    return VK_IMAGE_ASPECT_COLOR_BIT;
  }
  bool is_depth = isDepthFormat(format);
  bool is_stencil = isStencilFormat(format);
  VkImageAspectFlags aspects = 0;
  if (is_depth) {
    aspects |= VK_IMAGE_ASPECT_DEPTH_BIT;
  }
  if (is_stencil) {
    aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;
  }
  return aspects;
}

} // namespace ren
