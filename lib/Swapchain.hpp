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

class SwapchainImpl {
  VkSwapchainKHR m_swapchain = nullptr;
  SmallVector<AutoHandle<Texture>, 3> m_textures;
  u32 m_image_index = -1;
  VkSwapchainCreateInfoKHR m_create_info = {};

private:
  void create();
  void destroy();

public:
  SwapchainImpl(VkSurfaceKHR surface);
  SwapchainImpl(const SwapchainImpl &) = delete;
  SwapchainImpl(Swapchain &&) noexcept;
  SwapchainImpl &operator=(const SwapchainImpl &) = delete;
  SwapchainImpl &operator=(SwapchainImpl &&) noexcept;
  ~SwapchainImpl();

  std::tuple<u32, u32> get_size() const {
    return {m_create_info.imageExtent.width, m_create_info.imageExtent.height};
  }

  void set_size(u32 width, u32 height);

  auto get_present_mode() const -> VkPresentModeKHR {
    return m_create_info.presentMode;
  }

  void set_present_mode(VkPresentModeKHR);

  auto get_format() const -> VkFormat { return m_create_info.imageFormat; }

  auto get_surface() const -> VkSurfaceKHR { return m_create_info.surface; }

  auto acquire_texture(Handle<Semaphore> signal_semaphore) -> Handle<Texture>;

  void present(Handle<Semaphore> wait_semaphore);
};

} // namespace ren
