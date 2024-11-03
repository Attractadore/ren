#pragma once
#include "Semaphore.hpp"
#include "Support/Vector.hpp"
#include "Texture.hpp"

namespace ren {

class Renderer;

struct SwapchainTextureCreateInfo {
  VkImage image = nullptr;
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  VkImageUsageFlags usage = 0;
  u32 width = 0;
  u32 height = 1;
};

class Swapchain final : public ISwapchain {
  Renderer *m_renderer = nullptr;
  VkSwapchainKHR m_swapchain = nullptr;
  SmallVector<Handle<Texture>, 3> m_textures;
  u32 m_image_index = -1;
  VkSwapchainCreateInfoKHR m_create_info = {};

private:
  void create();
  void destroy();

public:
  Swapchain(Renderer &renderer, VkSurfaceKHR surface);
  Swapchain(const Swapchain &) = delete;
  Swapchain(Swapchain &&) noexcept;
  ~Swapchain();

  Swapchain &operator=(const Swapchain &) = delete;
  Swapchain &operator=(Swapchain &&) noexcept;

  auto get_size() const -> glm::uvec2 override {
    return {m_create_info.imageExtent.width, m_create_info.imageExtent.height};
  }

  auto set_size(unsigned width, unsigned height) -> expected<void> override;

  auto get_present_mode() const -> VkPresentModeKHR {
    return m_create_info.presentMode;
  }

  void set_present_mode(VkPresentModeKHR);

  auto get_format() const -> TinyImageFormat {
    return TinyImageFormat_FromVkFormat(
        (TinyImageFormat_VkFormat)m_create_info.imageFormat);
  }

  auto get_usage() const -> VkImageUsageFlags {
    return m_create_info.imageUsage;
  }

  auto get_surface() const -> VkSurfaceKHR { return m_create_info.surface; }

  auto acquire_texture(Handle<Semaphore> signal_semaphore) -> Handle<Texture>;

  void present(Handle<Semaphore> wait_semaphore);
};

} // namespace ren
