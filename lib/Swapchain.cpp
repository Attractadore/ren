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
  auto it = std::ranges::find_if(
      surface_formats, [](const VkSurfaceFormatKHR &surface_format) {
        return surface_format.format == VK_FORMAT_B8G8R8A8_SRGB;
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
  constexpr VkImageUsageFlags REQUIRED_USAGE = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  ren_assert((supported_usage & REQUIRED_USAGE) == REQUIRED_USAGE);
  return REQUIRED_USAGE;
}

auto get_optimal_image_count(WindowingSystem ws, VkPresentModeKHR pm,
                             bool is_fullscreen, u32 num_frames_in_flight)
    -> u32 {
  switch (ws) {
  default:
    return 3;
  case WindowingSystem::X11:
  case WindowingSystem::Wayland: {
    // On Linux, we need the following images:
    // 1. One for presenting.
    // 2. For mailbox, one queued for present.
    // 3. One for drawing into.
    // 4. One less than the number of frames in flight to record commands for
    // due to synchronous acquire.
    u32 num_images = num_frames_in_flight + 1;
    // Tearing is only allowed in fullscreen on Linux.
    if (pm == VK_PRESENT_MODE_MAILBOX_KHR or
        (pm == VK_PRESENT_MODE_IMMEDIATE_KHR and not is_fullscreen)) {
      return num_images + 1;
    }
    return num_images;
  }
  case WindowingSystem::Win32: {
    // On Windows, we need the following images:
    // 1. One for presenting.
    // 2. For mailbox, 1 or 2 queued for present. DWM can only returns images
    // that for queued for present, but were not presented, back to the swap
    // chain on the next vblank, which caps the maximum frame rate in mailbox to
    // refresh rate * (number of swap chain images - 1). flight count.
    // 3. One for drawing into.
    // 4. One less than the number of frames in flight to record commands for
    // due to synchronous acquire.
    u32 num_images = num_frames_in_flight + 1;
    // On Windows, tearing is allowed in windowed mode if MPOs are supported.
    if (pm == VK_PRESENT_MODE_MAILBOX_KHR) {
      return num_images + 1;
    }
    return num_images;
  }
  }
}

} // namespace

Swapchain::~Swapchain() {
  m_renderer->wait_idle();
  destroy();
  vkDestroySurfaceKHR(m_renderer->get_instance(), m_surface, nullptr);
}

void Swapchain::init(Renderer &renderer, VkSurfaceKHR surface,
                     const SurfaceCallbacks &cb, void *usrptr) {
  m_cb = cb;
  m_usrptr = usrptr;
  m_renderer = &renderer;
  m_surface = surface;
  m_size = m_cb.get_size(m_usrptr);
  m_fullscreen = m_cb.is_fullscreen(m_usrptr);
  recreate();
}

void Swapchain::set_vsync(VSync vsync) {
  if (m_vsync != vsync) {
    m_vsync = vsync;
    m_dirty = true;
  }
}

void Swapchain::set_frames_in_flight(u32 num_frames_in_flight) {
  if (m_num_frames_in_flight != num_frames_in_flight) {
    m_num_frames_in_flight = num_frames_in_flight;
    m_dirty = true;
  }
}

void Swapchain::recreate() {
  VkSurfaceCapabilitiesKHR capabilities =
      get_surface_capabilities(m_renderer->get_adapter(), m_surface);

  u32 num_present_modes = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(
      m_renderer->get_adapter(), m_surface, &num_present_modes, nullptr);
  SmallVector<VkPresentModeKHR> present_modes(num_present_modes);
  vkGetPhysicalDeviceSurfacePresentModesKHR(m_renderer->get_adapter(),
                                            m_surface, &num_present_modes,
                                            present_modes.data());

  VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
  if (m_vsync == VSync::Off) {
    bool have_immediate =
        std::ranges::find(present_modes, VK_PRESENT_MODE_IMMEDIATE_KHR) !=
        present_modes.end();
    bool have_mailbox =
        std::ranges::find(present_modes, VK_PRESENT_MODE_MAILBOX_KHR) !=
        present_modes.end();
    if (have_immediate) {
      present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    } else if (have_mailbox) {
      present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
    }
  }

  u32 num_images =
      get_optimal_image_count(m_cb.get_windowing_system(m_usrptr), present_mode,
                              m_fullscreen, m_num_frames_in_flight);
  num_images = std::max<u32>(capabilities.minImageCount, num_images);
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

  fmt::println("Create swap chain: {}x{}, fullscreen: {}, vsync: {}, {} images",
               m_size.x, m_size.y, m_fullscreen, m_vsync == VSync::On,
               num_images);

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
      .presentMode = present_mode,
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

auto Swapchain::acquire_texture(Handle<Semaphore> signal_semaphore)
    -> Handle<Texture> {
  ren_prof_zone("Swapchain::acquire_texture");

  glm::uvec2 size = m_cb.get_size(m_usrptr);
  if (m_size != size) {
    m_size = size;
    m_dirty = true;
  }

  bool fullscreen = m_cb.is_fullscreen(m_usrptr);
  if (m_fullscreen != fullscreen) {
    m_fullscreen = fullscreen;
    m_dirty = true;
  }

  if (m_dirty) {
    recreate();
  }

  while (true) {
    VkResult result = vkAcquireNextImageKHR(
        m_renderer->get_device(), m_swapchain, UINT64_MAX,
        m_renderer->get_semaphore(signal_semaphore).handle, nullptr,
        &m_image_index);
    switch (result) {
    case VK_SUCCESS:
    case VK_SUBOPTIMAL_KHR:
      return m_textures[m_image_index];
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
