#include "Swapchain.hpp"
#include "Device.hpp"
#include "Errors.hpp"
#include "Formats.inl"

#include <range/v3/algorithm.hpp>

namespace ren {

namespace {
VkSurfaceCapabilitiesKHR getSurfaceCapabilities(Device &device,
                                                VkSurfaceKHR surface) {
  VkSurfaceCapabilitiesKHR capabilities;
  throwIfFailed(
      device.GetPhysicalDeviceSurfaceCapabilitiesKHR(surface, &capabilities),
      "Vulkan: Failed to get surface capabilities");
  return capabilities;
}

auto getSurfaceFormats(Device &device, VkSurfaceKHR surface) {
  unsigned format_count = 0;
  throwIfFailed(device.GetPhysicalDeviceSurfaceFormatsKHR(
                    surface, &format_count, nullptr),
                "Vulkan: Failed to get surface formats");
  SmallVector<VkSurfaceFormatKHR, 8> formats(format_count);
  throwIfFailed(device.GetPhysicalDeviceSurfaceFormatsKHR(
                    surface, &format_count, formats.data()),
                "Vulkan: Failed to get surface formats");
  formats.resize(format_count);
  return formats;
}

VkSurfaceFormatKHR
selectSurfaceFormat(std::span<const VkSurfaceFormatKHR> surface_formats) {
  auto it = ranges::find_if(surface_formats, [](const VkSurfaceFormatKHR &sf) {
    return isSRGBFormat(sf.format);
  });
  return it != surface_formats.end() ? *it : surface_formats.front();
};

VkCompositeAlphaFlagBitsKHR
selectCompositeAlpha(VkCompositeAlphaFlagsKHR composite_alphas) {
  constexpr std::array preferred_order = {
      VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
      VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
      VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
  };

  for (auto composite_alpha : preferred_order) {
    if (composite_alphas & composite_alpha) {
      return composite_alpha;
    }
  }

  return static_cast<VkCompositeAlphaFlagBitsKHR>(composite_alphas &
                                                  ~(composite_alphas - 1));
}

VkImageUsageFlags BLIT_STRATEGY_USAGE = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
VkImageUsageFlags RENDER_STRATEGY_USAGE = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

VkImageUsageFlags selectImageUsage(VkImageUsageFlags image_usage) {
  const std::array preferred_order = {
      BLIT_STRATEGY_USAGE,
  };

  for (auto usage : preferred_order) {
    if ((usage & image_usage) == usage) {
      return usage;
    }
  }

  return RENDER_STRATEGY_USAGE;
}
} // namespace

Swapchain::Swapchain(Device &device, VkSurfaceKHR surface)
    : m_device(&device), m_create_info([&] {
        auto capabilities = getSurfaceCapabilities(*m_device, surface);
        auto surface_formats = getSurfaceFormats(*m_device, surface);

        auto image_count = std::max(capabilities.minImageCount, 2u);
        if (capabilities.maxImageCount) {
          image_count = std::min(capabilities.maxImageCount, image_count);
        }
        auto surface_format = selectSurfaceFormat(surface_formats);
        auto composite_alpha =
            selectCompositeAlpha(capabilities.supportedCompositeAlpha);
        auto image_usage = selectImageUsage(capabilities.supportedUsageFlags);

        return VkSwapchainCreateInfoKHR{
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface = surface,
            .minImageCount = image_count,
            .imageFormat = surface_format.format,
            .imageColorSpace = surface_format.colorSpace,
            .imageArrayLayers = 1,
            .imageUsage = image_usage,
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .compositeAlpha = composite_alpha,
            .presentMode = VK_PRESENT_MODE_FIFO_KHR,
            .clipped = true,
        };
      }()) {
  create();
}

Swapchain::~Swapchain() {
  destroy();
  m_device->push_to_delete_queue(m_create_info.surface);
}

void Swapchain::create() {
  auto capabilities = getSurfaceCapabilities(*m_device, m_create_info.surface);
  m_create_info.imageExtent = [&] {
    constexpr auto special_value = 0xFFFFFFFF;
    if (capabilities.currentExtent.width == special_value and
        capabilities.currentExtent.height == special_value) {
      return m_create_info.imageExtent;
    } else {
      return capabilities.currentExtent;
    }
  }();
  if (m_create_info.imageExtent.width == 0 or
      m_create_info.imageExtent.height == 0) {
    return;
  }
  m_create_info.preTransform = capabilities.currentTransform;
  m_create_info.oldSwapchain = m_swapchain;

  VkSwapchainKHR new_swapchain;
  throwIfFailed(m_device->CreateSwapchainKHR(&m_create_info, &new_swapchain),
                "Vulkan: Failed to create swapchain");
  destroy();
  m_swapchain = new_swapchain;

  unsigned image_count = 0;
  throwIfFailed(
      m_device->GetSwapchainImagesKHR(m_swapchain, &image_count, nullptr),
      "Vulkan: Failed to get swapchain image count");
  SmallVector<VkImage, 3> images(image_count);
  throwIfFailed(
      m_device->GetSwapchainImagesKHR(m_swapchain, &image_count, images.data()),
      "Vulkan: Failed to get swapchain images");

  m_textures.resize(image_count);
  ranges::transform(images, m_textures.begin(), [&](VkImage image) {
    return m_device->create_swapchain_texture({
        .image = image,
        .format = m_create_info.imageFormat,
        .usage = m_create_info.imageUsage,
        .width = m_create_info.imageExtent.width,
        .height = m_create_info.imageExtent.height,
    });
  });
}

void Swapchain::destroy() {
  m_device->push_to_delete_queue(m_swapchain);
  for (auto texture : m_textures) {
    m_device->destroy_texture(texture);
  }
  m_textures.clear();
}

void Swapchain::set_size(unsigned width, unsigned height) noexcept {
  m_create_info.imageExtent = {
      .width = width,
      .height = height,
  };
}

void Swapchain::set_present_mode(VkPresentModeKHR present_mode) { todo(); }

void Swapchain::acquireImage(Handle<Semaphore> signal_semaphore) {
  while (true) {
    auto r = m_device->AcquireNextImageKHR(
        m_swapchain, UINT64_MAX,
        m_device->get_semaphore(signal_semaphore).handle, nullptr,
        &m_image_index);
    switch (r) {
    case VK_SUCCESS:
    case VK_SUBOPTIMAL_KHR:
      return;
    case VK_ERROR_OUT_OF_DATE_KHR:
      create();
      continue;
    default:
      throw std::runtime_error{"Vulkan: Failed to acquire image"};
    }
  }
}

void Swapchain::presentImage(Handle<Semaphore> wait_semaphore) {
  VkPresentInfoKHR present_info = {
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &m_device->get_semaphore(wait_semaphore).handle,
      .swapchainCount = 1,
      .pSwapchains = &m_swapchain,
      .pImageIndices = &m_image_index,
  };
  auto r = m_device->queuePresent(present_info);
  switch (r) {
  case VK_SUCCESS:
    return;
  case VK_SUBOPTIMAL_KHR:
  case VK_ERROR_OUT_OF_DATE_KHR:
    create();
    return;
  default:
    throw std::runtime_error{"Vulkan: Failed to present image"};
  }
}

auto Swapchain::getTexture() const -> TextureView {
  return m_device->get_texture_view(m_textures[m_image_index]);
}

} // namespace ren
