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
public:
  Swapchain() = default;
  Swapchain(const Swapchain &) = delete;
  Swapchain(Swapchain &&) noexcept;
  ~Swapchain();

  Swapchain &operator=(const Swapchain &) = delete;
  Swapchain &operator=(Swapchain &&) noexcept;

  void init(Renderer &renderer, VkSurfaceKHR surface);

  auto get_size() const -> glm::uvec2 override { return m_size; }

  void set_size(unsigned width, unsigned height) override;

  auto get_num_textures() const { return m_textures.size(); }

  auto get_present_mode() const -> VkPresentModeKHR { return m_present_mode; }

  void set_present_mode(VkPresentModeKHR);

  auto get_format() const -> TinyImageFormat { return m_format; }

  auto get_usage() const -> VkImageUsageFlags { return m_usage; }

  auto get_surface() const -> VkSurfaceKHR { return m_surface; }

  auto acquire_texture(Handle<Semaphore> signal_semaphore) -> Handle<Texture>;

  void present(Handle<Semaphore> wait_semaphore);

private:
  void recreate();
  void destroy();

private:
  Renderer *m_renderer = nullptr;
  VkSurfaceKHR m_surface = nullptr;
  VkSwapchainKHR m_swapchain = nullptr;
  SmallVector<Handle<Texture>> m_textures;
  glm::uvec2 m_size = {};
  VkPresentModeKHR m_present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
  TinyImageFormat m_format = TinyImageFormat_UNDEFINED;
  VkColorSpaceKHR m_color_space = {};
  VkImageUsageFlags m_usage = 0;
  u32 m_image_index = -1;
  u8 m_num_images = 0;
  bool m_dirty = true;
};

} // namespace ren
