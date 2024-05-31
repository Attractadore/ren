#include "ren/ren-sdl2.hpp"
#include "Lippincott.hpp"
#include "Renderer.hpp"
#include "Support/Vector.hpp"
#include "Swapchain.hpp"

#include <SDL2/SDL_vulkan.h>

auto ren::sdl2::create_renderer(unsigned adapter)
    -> expected<std::unique_ptr<IRenderer>> {
  if (SDL_Vulkan_LoadLibrary(nullptr)) {
    return std::unexpected(Error::SDL2);
  }

  uint32_t num_extensions = 0;
  if (!SDL_Vulkan_GetInstanceExtensions(nullptr, &num_extensions, nullptr)) {
    return std::unexpected(Error::SDL2);
  }
  SmallVector<const char *, 4> extensions(num_extensions);
  if (!SDL_Vulkan_GetInstanceExtensions(nullptr, &num_extensions,
                                        extensions.data())) {
    return std::unexpected(Error::SDL2);
  }

  return ren::create_renderer({
      .vk_instance_extensions = extensions,
      .adapter = adapter,
  });
}

auto ren::sdl2::get_required_window_flags(IRenderer &) -> Uint32 {
  return SDL_WINDOW_VULKAN;
}

auto ren::sdl2::create_swapchain(IRenderer &irenderer, SDL_Window *window)
    -> expected<std::unique_ptr<ISwapchain>> {
  auto &renderer = static_cast<Renderer &>(irenderer);

  VkSurfaceKHR surface;
  if (!SDL_Vulkan_CreateSurface(window, renderer.get_instance(), &surface)) {
    return std::unexpected(Error::Vulkan);
  }

  return lippincott(
      [&] { return std::make_unique<Swapchain>(renderer, surface); });
}
