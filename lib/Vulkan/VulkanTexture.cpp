#include "Vulkan/VulkanTexture.inl"
#include "Support/Enum.hpp"
#include "Support/Errors.hpp"
#include "Vulkan/VulkanDevice.hpp"
#include "Vulkan/VulkanFormats.hpp"

namespace ren {
namespace {
constexpr std::array texture_type_map = {
    std::pair(TextureType::e1D, VK_IMAGE_TYPE_1D),
    std::pair(TextureType::e2D, VK_IMAGE_TYPE_2D),
    std::pair(TextureType::e3D, VK_IMAGE_TYPE_3D),
};

constexpr auto getVkImageType = enumMap<texture_type_map>;

constexpr std::array texture_view_type_map = {
    std::pair(TextureViewType::e2D, VK_IMAGE_VIEW_TYPE_2D),
};

constexpr auto getVkImageViewType = enumMap<texture_view_type_map>;
} // namespace

VulkanTexture::VulkanTexture(VulkanDevice *device, VkImage image,
                             const TextureDesc &desc)
    : m_device(device), m_image(image), m_desc(desc) {
  assert(m_device);
  assert(m_image);
}

VulkanTexture::VulkanTexture(VulkanDevice *device, VmaAllocator allocator,
                             const TextureDesc &desc)
    : m_device(device), m_allocator(allocator), m_desc(desc) {
  assert(m_device);
  assert(m_allocator);

  VkImageCreateInfo image_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = getVkImageType(desc.type),
      .format = getVkFormat(desc.format),
      .extent = {desc.width, desc.height, desc.depth},
      .mipLevels = desc.levels,
      .arrayLayers = desc.layers,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = getVkImageUsageFlags(desc.usage),
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };

  VmaAllocationCreateInfo alloc_info = {.usage = VMA_MEMORY_USAGE_AUTO};

  throwIfFailed(vmaCreateImage(allocator, &image_info, &alloc_info, &m_image,
                               &m_allocation, nullptr),
                "VMA: Failed to create image");
}

VulkanTexture::~VulkanTexture() {
  for (auto view : m_views.values()) {
    m_device->DestroyImageView(view);
  }
  if (m_allocation) {
    vmaDestroyImage(m_allocator, m_image, m_allocation);
  }
}

namespace {
VkImageView createView(VulkanDevice *device, VkImage image,
                       const TextureDesc &tex_desc,
                       const TextureViewDesc &view_desc) {
  VkImageViewCreateInfo view_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = image,
      .viewType = getVkImageViewType(view_desc.type),
      .format = getVkFormat(tex_desc.format),
      .subresourceRange =
          {
              .aspectMask = getFormatAspectFlags(tex_desc.format),
              .baseMipLevel = view_desc.subresource.first_mip_level,
              .levelCount = view_desc.subresource.mip_level_count,
              .baseArrayLayer = view_desc.subresource.first_layer,
              .layerCount = view_desc.subresource.layer_count,
          },
  };
  VkImageView view;
  throwIfFailed(device->CreateImageView(&view_info, &view),
                "Vulkan: Failed to create image view");
  return view;
}
} // namespace

VkImageView VulkanTexture::getView(const TextureViewDesc &view_desc) {
  auto it = m_views.find(view_desc);
  if (it != m_views.end()) {
    auto &&[desc, view] = *it;
    return view;
  }
  auto view = createView(m_device, m_image, m_desc, view_desc);
  m_views.insert(view_desc, view);
  return view;
}
} // namespace ren
