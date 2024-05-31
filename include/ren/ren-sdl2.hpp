#pragma once
#include "ren.hpp"

#include <SDL2/SDL.h>

namespace ren::sdl2 {

[[nodiscard]] auto create_renderer(unsigned adapter = 0)
    -> expected<std::unique_ptr<IRenderer>>;

[[nodiscard]] auto get_required_window_flags(IRenderer &renderer) -> Uint32;

[[nodiscard]] auto create_swapchain(IRenderer &renderer, SDL_Window *window)
    -> expected<std::unique_ptr<ISwapchain>>;

} // namespace ren::sdl2
