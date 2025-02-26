#pragma once
#include "Semaphore.hpp"
#include "Texture.hpp"
#include "core/Vector.hpp"
#include "rhi.hpp"

namespace ren {

class Renderer;

class Swapchain final : public ISwapchain {
public:
  Swapchain() = default;
  Swapchain(const Swapchain &) = delete;
  Swapchain(Swapchain &&) noexcept;
  ~Swapchain();

  Swapchain &operator=(const Swapchain &) = delete;
  Swapchain &operator=(Swapchain &&) noexcept;

  auto init(Renderer &renderer, SDL_Window *window) -> Result<void, Error>;

  void set_vsync(VSync vsync) override;

  auto get_size() const -> glm::uvec2 { return m_size; }

  auto get_format() const -> TinyImageFormat { return m_format; }

  auto get_usage() const -> rhi::ImageUsageFlags { return m_usage; }

  void set_usage(rhi::ImageUsageFlags usage);

  auto get_queue_family() const -> rhi::QueueFamily { return m_queue_family; }

  auto acquire_texture(Handle<Semaphore> signal_semaphore)
      -> Result<Handle<Texture>, Error>;

  auto present(Handle<Semaphore> wait_semaphore) -> Result<void, Error>;

private:
  auto select_present_mode() -> Result<rhi::PresentMode, Error>;
  auto select_image_count(rhi::PresentMode present_mode) -> Result<u32, Error>;
  auto update_textures() -> Result<void, Error>;
  auto update() -> Result<void, Error>;

private:
  Renderer *m_renderer = nullptr;
  SDL_Window *m_window = nullptr;
  rhi::Surface m_surface = {};
  rhi::SwapChain m_swap_chain = {};
  rhi::QueueFamily m_queue_family = {};
  SmallVector<Handle<Texture>> m_textures;
  TinyImageFormat m_format = TinyImageFormat_UNDEFINED;
  rhi::ImageUsageFlags m_usage = {};
  glm::ivec2 m_size = {};
  VSync m_vsync = VSync::Off;
  bool m_fullscreen = false;
  u32 m_image_index = -1;
  bool m_dirty = false;
};

} // namespace ren
