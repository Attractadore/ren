#pragma once
#include "Support/Enum.hpp"
#include "Texture.hpp"

#include <vulkan/vulkan.h>

namespace ren {
namespace detail {
template <> struct FlagsTypeImpl<VkImageUsageFlagBits> {
  using type = VkImageUsageFlags;
};

constexpr std::array texture_type_map = {
    std::pair(TextureType::e1D, VK_IMAGE_TYPE_1D),
    std::pair(TextureType::e2D, VK_IMAGE_TYPE_2D),
    std::pair(TextureType::e3D, VK_IMAGE_TYPE_3D),
};

constexpr std::array usage_flags_map = {
    std::pair(TextureUsage::RenderTarget, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT),
    std::pair(TextureUsage::TransferSRC, VK_IMAGE_USAGE_TRANSFER_SRC_BIT),
    std::pair(TextureUsage::TransferDST, VK_IMAGE_USAGE_TRANSFER_DST_BIT),
};

constexpr std::array texture_view_type_map = {
    std::pair(TextureViewType::e2D, VK_IMAGE_VIEW_TYPE_2D),
};
} // namespace detail

constexpr auto getVkImageType = enumMap<detail::texture_type_map>;
constexpr auto getTextureType = inverseEnumMap<detail::texture_type_map>;

constexpr auto getVkImageUsageFlags = flagsMap<detail::usage_flags_map>;
constexpr auto getTextureUsageFlags = inverseFlagsMap<detail::usage_flags_map>;

constexpr auto getVkImageViewType = enumMap<detail::texture_view_type_map>;
constexpr auto getTextureViewType =
    inverseEnumMap<detail::texture_view_type_map>;

inline VkImage getVkImage(const Texture &tex) {
  return reinterpret_cast<VkImage>(tex.handle.get());
}
} // namespace ren
