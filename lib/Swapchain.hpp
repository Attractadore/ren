#pragma once
#include "Semaphore.hpp"
#include "Texture.hpp"
#include "core/Vector.hpp"

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

  void init(Renderer &renderer, VkSurfaceKHR surface,
            const SurfaceCallbacks &cb, void *usrptr);

  void set_vsync(VSync vsync) override;

  void set_frames_in_flight(u32 num_frames_in_flight);

  auto get_size() const -> glm::uvec2 { return m_size; }

  auto get_format() const -> TinyImageFormat { return m_format; }

  auto get_usage() const -> VkImageUsageFlags { return m_usage; }

  auto get_surface() const -> VkSurfaceKHR { return m_surface; }

  auto acquire_texture(Handle<Semaphore> signal_semaphore) -> Handle<Texture>;

  void present(Handle<Semaphore> wait_semaphore);

private:
  void recreate();
  void destroy();

private:
  SurfaceCallbacks m_cb;
  void *m_usrptr = nullptr;
  Renderer *m_renderer = nullptr;
  VkSurfaceKHR m_surface = nullptr;
  VkSwapchainKHR m_swapchain = nullptr;
  SmallVector<Handle<Texture>> m_textures;
  glm::uvec2 m_size = {};
  VSync m_vsync = VSync::Off;
  bool m_fullscreen = false;
  TinyImageFormat m_format = TinyImageFormat_UNDEFINED;
  VkColorSpaceKHR m_color_space = {};
  VkImageUsageFlags m_usage = 0;
  u32 m_image_index = -1;
  u32 m_num_frames_in_flight = 2;
  bool m_dirty = true;
};

} // namespace ren
