#pragma once
#include "ren.hpp"

#include <SDL2/SDL.h>

namespace ren::sdl2 {

struct InitInfo {
  unsigned adapter = 0;
};

[[nodiscard]] auto init(const InitInfo &init_info = {}) -> expected<void>;

[[nodiscard]] auto get_window_flags() -> Uint32;

[[nodiscard]] auto create_swapchain(SDL_Window *window)
    -> expected<std::unique_ptr<Swapchain>>;

} // namespace ren::sdl2
