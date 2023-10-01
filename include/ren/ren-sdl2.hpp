#pragma once
#include "ren.hpp"

#include <SDL2/SDL.h>

namespace ren::sdl2 {

struct RendererDesc {
  unsigned adapter = 0;
};

[[nodiscard]] auto init(const RendererDesc &desc = {}) -> expected<void>;

[[nodiscard]] auto get_window_flags() -> Uint32;

[[nodiscard]] auto create_swapchain(SDL_Window *window) -> expected<Swapchain>;

} // namespace ren::sdl2
