#include "ren/ren.hpp"
#include "Renderer.hpp"
#include "Swapchain.hpp"

namespace ren {

auto create_renderer(u32 adapter) -> expected<std::unique_ptr<IRenderer>> {
  auto renderer = std::make_unique<Renderer>();
  return renderer->init(adapter).transform([&] { return std::move(renderer); });
}

auto get_sdl_window_flags(IRenderer &) -> uint32_t {
  return rhi::SDL_WINDOW_FLAGS;
}

auto create_swapchain(IRenderer &irenderer, SDL_Window *window)
    -> expected<std::unique_ptr<ISwapchain>> {
  auto &renderer = static_cast<Renderer &>(irenderer);
  auto swapchain = std::make_unique<Swapchain>();
  return swapchain->init(renderer, window).transform([&] {
    return std::move(swapchain);
  });
}

} // namespace ren
