#pragma once
#include "Semaphore.hpp"
#include "Texture.hpp"
#include "rhi.hpp"

namespace ren {

class Renderer;

struct SwapChain {
  auto init(NotNull<Arena *> arena, Renderer &renderer, SDL_Window *window)
      -> Result<void, Error>;

  auto get_size() const -> glm::uvec2 { return m_size; }

  auto get_format() const -> TinyImageFormat { return m_format; }

  auto get_usage() const -> rhi::ImageUsageFlags { return m_usage; }

  void set_usage(rhi::ImageUsageFlags usage);

  auto acquire(Handle<Semaphore> signal_semaphore) -> Result<u32, Error>;

  auto get_texture(u32 i) -> Handle<Texture> {
    ren_assert(i < m_num_textures);
    return m_textures[i];
  }

  auto get_semaphore(u32 i) -> Handle<Semaphore> {
    ren_assert(i < m_num_textures);
    return m_semaphores[i];
  }

  auto present(rhi::QueueFamily queue_family) -> Result<void, Error>;

  auto is_queue_family_supported(rhi::QueueFamily queue_family) const -> bool;

  auto select_present_mode() -> Result<rhi::PresentMode, Error>;
  auto select_image_count(rhi::PresentMode present_mode) -> Result<u32, Error>;
  auto update_textures() -> Result<void, Error>;
  auto update() -> Result<void, Error>;

  Renderer *m_renderer = nullptr;
  SDL_Window *m_window = nullptr;
  rhi::Surface m_surface = {};
  rhi::SwapChain m_swap_chain = {};
  u32 m_num_textures = 0;
  Handle<Texture> m_textures[8] = {};
  Handle<Semaphore> m_semaphores[8] = {};
  TinyImageFormat m_format = TinyImageFormat_UNDEFINED;
  rhi::ImageUsageFlags m_usage = {};
  glm::ivec2 m_size = {};
  VSync m_vsync = VSync::Off;
  bool m_fullscreen = false;
  u32 m_image_index = -1;
  bool m_dirty = false;
};

} // namespace ren
