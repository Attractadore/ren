#pragma once
#include "Support/LinearMap.hpp"
#include "Texture.hpp"
#include "vma.h"

namespace ren {
class VulkanDevice;

class VulkanTexture {
  VulkanDevice *m_device = nullptr;
  VmaAllocator m_allocator = VK_NULL_HANDLE;
  VkImage m_image = VK_NULL_HANDLE;
  VmaAllocation m_allocation = VK_NULL_HANDLE;
  LinearMap<TextureViewDesc, VkImageView, 3> m_views;
  TextureDesc m_desc;

public:
  VulkanTexture(VulkanDevice *device, VkImage image, const TextureDesc &desc);
  VulkanTexture(VulkanDevice *device, VmaAllocator allocator,
                const TextureDesc &desc);
  ~VulkanTexture();

  VkImage getImage() const { return m_image; }
  VkImageView getView(const TextureViewDesc &view_desc);
};
} // namespace ren
