#pragma once
#include "VulkanTexture.hpp"
#include "Support/Enum.hpp"

namespace ren {
namespace detail {
template <> struct FlagsTypeImpl<VkImageUsageFlagBits> {
  using type = VkImageUsageFlags;
};

constexpr std::array usage_flags_map = {
    std::pair(TextureUsage::RenderTarget, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT),
    std::pair(TextureUsage::TransferSRC, VK_IMAGE_USAGE_TRANSFER_SRC_BIT),
    std::pair(TextureUsage::TransferDST, VK_IMAGE_USAGE_TRANSFER_DST_BIT),
};
} // namespace detail

constexpr auto getVkImageUsageFlags = flagsMap<detail::usage_flags_map>;
constexpr auto getTextureUsageFlags = inverseFlagsMap<detail::usage_flags_map>;

inline VulkanTexture* getVulkanTexture(const Texture& tex) {
  return reinterpret_cast<VulkanTexture*>(tex.handle.get());
}

inline VkImageView getVkTextureView(const TextureView& view) {
  return getVulkanTexture(view.texture)->getView(view.desc);
}

inline VkImage getVkImage(const Texture& tex) {
  return getVulkanTexture(tex)->getImage();
}
} // namespace ren
