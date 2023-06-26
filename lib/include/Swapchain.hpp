#pragma once
#include "Semaphore.hpp"
#include "Support/Vector.hpp"
#include "Texture.hpp"

#include <utility>

namespace ren {

class Device;

struct SwapchainTextureCreateInfo {
  VkImage image = nullptr;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkImageUsageFlags usage = 0;
  glm::uvec2 size = {0, 0};
};

class Swapchain {
  Device *m_device;
  VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
  SmallVector<Handle<Texture>, 3> m_textures;
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

  std::pair<unsigned, unsigned> get_size() const noexcept {
    return {m_create_info.imageExtent.width, m_create_info.imageExtent.height};
  }

  void set_size(unsigned width, unsigned height) noexcept;

  VkPresentModeKHR get_present_mode() const noexcept {
    return m_create_info.presentMode;
  }

  void set_present_mode(VkPresentModeKHR);

  VkSurfaceKHR get_surface() const noexcept { return m_create_info.surface; }

  void acquireImage(Handle<Semaphore> signal_semaphore);
  void presentImage(Handle<Semaphore> wait_semaphore);

  auto getTexture() const -> TextureView;
};

} // namespace ren

struct RenSwapchain : ren::Swapchain {
  using ren::Swapchain::Swapchain;
};
