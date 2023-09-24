#pragma once
#include "Semaphore.hpp"
#include "Support/Vector.hpp"
#include "Texture.hpp"

#include <tuple>

namespace ren {

struct SwapchainTextureCreateInfo {
  VkImage image = nullptr;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkImageUsageFlags usage = 0;
  u32 width = 0;
  u32 height = 1;
};

class Swapchain {
  VkSwapchainKHR m_swapchain = nullptr;
  SmallVector<Handle<Texture>, 3> m_textures;
  u32 m_image_index = -1;
  VkSwapchainCreateInfoKHR m_create_info = {};

private:
  void create();
  void destroy();

public:
  Swapchain(VkSurfaceKHR surface);
  Swapchain(const Swapchain &) = delete;
  Swapchain(Swapchain &&);
  Swapchain &operator=(const Swapchain &) = delete;
  Swapchain &operator=(Swapchain &&);
  ~Swapchain();

  std::tuple<u32, u32> get_size() const noexcept {
    return {m_create_info.imageExtent.width, m_create_info.imageExtent.height};
  }

  void set_size(u32 width, u32 height) noexcept;

  auto get_present_mode() const noexcept -> VkPresentModeKHR {
    return m_create_info.presentMode;
  }

  void set_present_mode(VkPresentModeKHR);

  auto get_surface() const noexcept -> VkSurfaceKHR {
    return m_create_info.surface;
  }

  auto acquire_texture(Handle<Semaphore> signal_semaphore) -> Handle<Texture>;

  void present(Handle<Semaphore> wait_semaphore);
};

} // namespace ren

struct RenSwapchain : ren::Swapchain {
  using ren::Swapchain::Swapchain;
};
