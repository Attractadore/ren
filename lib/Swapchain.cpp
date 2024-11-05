#include "Swapchain.hpp"
#include "Profiler.hpp"
#include "Renderer.hpp"
#include "Support/Errors.hpp"

#include <algorithm>

namespace ren {

namespace {

auto get_surface_capabilities(VkPhysicalDevice adapter, VkSurfaceKHR surface)
    -> VkSurfaceCapabilitiesKHR {
  VkSurfaceCapabilitiesKHR capabilities;
  throw_if_failed(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(adapter, surface,
                                                            &capabilities),
                  "Vulkan: Failed to get surface capabilities");
  return capabilities;
}

auto select_surface_format(Span<const VkSurfaceFormatKHR> surface_formats)
    -> VkSurfaceFormatKHR {
  auto it =
      std::find_if(surface_formats.begin(), surface_formats.end(),
                   [](const VkSurfaceFormatKHR &surface_format) {
                     TinyImageFormat format = TinyImageFormat_FromVkFormat(
                         (TinyImageFormat_VkFormat)surface_format.format);
                     return TinyImageFormat_IsSRGB(format);
                   });
  return it != surface_formats.end() ? *it : surface_formats.front();
};

auto select_composite_alpha(VkCompositeAlphaFlagsKHR composite_alpha_flags)
    -> VkCompositeAlphaFlagBitsKHR {
  constexpr std::array PREFERRED_ORDER = {
      VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
      VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
      VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
  };

  for (VkCompositeAlphaFlagBitsKHR composite_alpha : PREFERRED_ORDER) {
    if (composite_alpha_flags & composite_alpha) {
      return composite_alpha;
    }
  }

  std::unreachable();
}

auto select_image_usage(VkImageUsageFlags supported_usage)
    -> VkImageUsageFlags {
  constexpr VkImageUsageFlags REQUIRED_USAGE =
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  ren_assert((supported_usage & REQUIRED_USAGE) == REQUIRED_USAGE);
  return REQUIRED_USAGE;
}

} // namespace

Swapchain::~Swapchain() {
  m_renderer->wait_idle();
  destroy();
  vkDestroySurfaceKHR(m_renderer->get_instance(), m_surface, nullptr);
}

void Swapchain::init(Renderer &renderer, VkSurfaceKHR surface) {
  m_renderer = &renderer;
  m_surface = surface;
  recreate();
}

void Swapchain::recreate() {
  VkSurfaceCapabilitiesKHR capabilities =
      get_surface_capabilities(m_renderer->get_adapter(), m_surface);

  auto num_images = std::max<u32>(capabilities.minImageCount, m_num_images);
  if (capabilities.maxImageCount) {
    num_images = std::min<u32>(capabilities.maxImageCount, num_images);
  }

  SmallVector<VkSurfaceFormatKHR> surface_formats;
  {
    uint32_t num_formats = 0;
    throw_if_failed(
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_renderer->get_adapter(),
                                             m_surface, &num_formats, nullptr),
        "Vulkan: Failed to get surface formats");
    surface_formats.resize(num_formats);
    throw_if_failed(vkGetPhysicalDeviceSurfaceFormatsKHR(
                        m_renderer->get_adapter(), m_surface, &num_formats,
                        surface_formats.data()),
                    "Vulkan: Failed to get surface formats");
    surface_formats.resize(num_formats);
  }
  VkSurfaceFormatKHR surface_format = select_surface_format(surface_formats);
  m_format = TinyImageFormat_FromVkFormat(
      (TinyImageFormat_VkFormat)surface_format.format);
  m_color_space = surface_format.colorSpace;

  if (capabilities.currentExtent.width != 0xFFFFFFFF) {
    m_size.x = capabilities.currentExtent.width;
  }
  if (capabilities.currentExtent.height != 0xFFFFFFFF) {
    m_size.y = capabilities.currentExtent.height;
  }
  if (m_size.x == 0 or m_size.y == 0) {
    return;
  }

  m_usage = select_image_usage(capabilities.supportedUsageFlags);

  VkCompositeAlphaFlagBitsKHR composite_alpha =
      select_composite_alpha(capabilities.supportedCompositeAlpha);

  VkSwapchainCreateInfoKHR create_info = {
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface = m_surface,
      .minImageCount = num_images,
      .imageFormat = surface_format.format,
      .imageColorSpace = surface_format.colorSpace,
      .imageExtent = {m_size.x, m_size.y},
      .imageArrayLayers = 1,
      .imageUsage = m_usage,
      .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .preTransform = capabilities.currentTransform,
      .compositeAlpha = composite_alpha,
      .presentMode = m_present_mode,
      .clipped = true,
      .oldSwapchain = m_swapchain,
  };

  VkSwapchainKHR new_swapchain;
  throw_if_failed(vkCreateSwapchainKHR(m_renderer->get_device(), &create_info,
                                       nullptr, &new_swapchain),
                  "Vulkan: Failed to create swapchain");
  m_renderer->wait_idle();
  destroy();
  m_swapchain = new_swapchain;

  throw_if_failed(vkGetSwapchainImagesKHR(m_renderer->get_device(), m_swapchain,
                                          &num_images, nullptr),
                  "Vulkan: Failed to get swapchain image count");
  SmallVector<VkImage> images(num_images);
  throw_if_failed(vkGetSwapchainImagesKHR(m_renderer->get_device(), m_swapchain,
                                          &num_images, images.data()),
                  "Vulkan: Failed to get swapchain images");

  m_num_images = num_images;
  m_textures.resize(num_images);
  for (usize i = 0; i < num_images; ++i) {
    m_textures[i] = m_renderer->create_swapchain_texture({
        .image = images[i],
        .format = m_format,
        .usage = m_usage,
        .width = m_size.x,
        .height = m_size.y,
    });
  }

  m_dirty = false;
}

void Swapchain::destroy() {
  vkDestroySwapchainKHR(m_renderer->get_device(), m_swapchain, nullptr);
  for (Handle<Texture> t : m_textures) {
    m_renderer->destroy(t);
  }
  m_textures.clear();
}

void Swapchain::set_size(unsigned width, unsigned height) {
  if (m_size.x != width or m_size.y != height) {
    m_size = {width, height};
    // Reset image count when changing size to save some memory if we don't need
    // as many images as previously (e.g when switching from windowed to
    // fullscreen, we need 1 less image since the compositor is disabled).
    m_num_images = 0;
    m_dirty = true;
  }
}

void Swapchain::set_present_mode(VkPresentModeKHR present_mode) { todo(); }

auto Swapchain::acquire_texture(Handle<Semaphore> signal_semaphore)
    -> Handle<Texture> {
  ren_prof_zone("Swapchain::acquire_texture");
  if (m_dirty) {
    recreate();
  }
  while (true) {
    constexpr u64 TIMEOUT = 500'000;
    VkResult result = vkAcquireNextImageKHR(
        m_renderer->get_device(), m_swapchain, TIMEOUT,
        m_renderer->get_semaphore(signal_semaphore).handle, nullptr,
        &m_image_index);
    switch (result) {
    case VK_SUCCESS:
    case VK_SUBOPTIMAL_KHR:
      return m_textures[m_image_index];
    case VK_NOT_READY:
    case VK_TIMEOUT:
      // We are blocked by the presentation engine. Try to use more images to
      // fix this.
      m_num_images++;
      recreate();
      continue;
    case VK_ERROR_OUT_OF_DATE_KHR:
      recreate();
      continue;
    default:
      throw_if_failed(result, "Vulkan: Failed to acquire image");
    }
  }
}

void Swapchain::present(Handle<Semaphore> wait_semaphore) {
  ren_prof_zone("Swapchain::present");
  VkPresentInfoKHR present_info = {
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &m_renderer->get_semaphore(wait_semaphore).handle,
      .swapchainCount = 1,
      .pSwapchains = &m_swapchain,
      .pImageIndices = &m_image_index,
  };
  VkResult result = m_renderer->queue_present(present_info);
  switch (result) {
  default:
    throw_if_failed(result, "Vulkan: Failed to present image");
  case VK_SUCCESS:
    return;
  case VK_SUBOPTIMAL_KHR:
  case VK_ERROR_OUT_OF_DATE_KHR:
    recreate();
    return;
  }
}

} // namespace ren
