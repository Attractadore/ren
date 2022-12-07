#pragma once
#include "Texture.hpp"

#include <vulkan/vulkan.h>

namespace ren {
REN_MAP_TYPE(TextureType, VkImageType);
REN_MAP_FIELD(TextureType::e1D, VK_IMAGE_TYPE_1D);
REN_MAP_FIELD(TextureType::e2D, VK_IMAGE_TYPE_2D);
REN_MAP_FIELD(TextureType::e3D, VK_IMAGE_TYPE_3D);

REN_MAP_ENUM(getVkImageType, TextureType, REN_TEXTURE_TYPES);
REN_REVERSE_MAP_ENUM(getTextureType, TextureType, REN_TEXTURE_TYPES);

REN_MAP_TYPE(TextureUsage, VkImageUsageFlagBits);
REN_ENUM_FLAGS(VkImageUsageFlagBits, VkImageUsageFlags);
REN_MAP_FIELD(TextureUsage::RenderTarget, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
REN_MAP_FIELD(TextureUsage::DepthStencilTarget,
              VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
REN_MAP_FIELD(TextureUsage::TransferSRC, VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
REN_MAP_FIELD(TextureUsage::TransferDST, VK_IMAGE_USAGE_TRANSFER_DST_BIT);
REN_MAP_FIELD(TextureUsage::Storage, VK_IMAGE_USAGE_STORAGE_BIT);
REN_MAP_FIELD(TextureUsage::Sampled, VK_IMAGE_USAGE_SAMPLED_BIT);

REN_MAP_ENUM_AND_FLAGS(getVkImageUsage, TextureUsage, REN_TEXTURE_USAGES);
REN_REVERSE_MAP_ENUM_AND_FLAGS(getTextureUsage, TextureUsage,
                               REN_TEXTURE_USAGES);

REN_MAP_TYPE(TextureViewType, VkImageViewType);
REN_MAP_FIELD(TextureViewType::e2D, VK_IMAGE_VIEW_TYPE_2D);

REN_MAP_ENUM(getVkImageViewType, TextureViewType, REN_TEXTURE_VIEW_TYPES);
REN_REVERSE_MAP_ENUM(getTextureViewType, TextureViewType,
                     REN_TEXTURE_VIEW_TYPES);

inline VkImage getVkImage(const Texture &tex) {
  return reinterpret_cast<VkImage>(tex.handle.get());
}
} // namespace ren
