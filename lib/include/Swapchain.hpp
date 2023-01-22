#pragma once
#include "Support/Vector.hpp"
#include "Texture.hpp"

#include <utility>

namespace ren {

class Device;

class Swapchain {
  Device *m_device;
  VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
  SmallVector<Texture, 3> m_textures;
  unsigned m_image_index = -1;
  VkSwapchainCreateInfoKHR m_create_info;

private:
  void create();
  void destroy();

public:
  Swapchain(Device &device, VkSurfaceKHR surface);
  Swapchain(const Swapchain &) = delete;
  Swapchain(Swapchain &&);
  Swapchain &operator=(const Swapchain &) = delete;
  Swapchain &operator=(Swapchain &&);
  ~Swapchain();

  std::pair<unsigned, unsigned> get_size() const {
    return {m_create_info.imageExtent.width, m_create_info.imageExtent.height};
  }
  void set_size(unsigned width, unsigned height);

  VkPresentModeKHR get_present_mode() const {
    return m_create_info.presentMode;
  }
  void set_present_mode(VkPresentModeKHR);

  VkSurfaceKHR get_surface() const { return m_create_info.surface; }

  void acquireImage(VkSemaphore signal_semaphore);
  void presentImage(VkSemaphore wait_semaphore);
  const Texture &getTexture() {
    assert(m_image_index < m_textures.size());
    return m_textures[m_image_index];
  }
};

} // namespace ren

struct RenSwapchain : ren::Swapchain {
  using ren::Swapchain::Swapchain;
};
