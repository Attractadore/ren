#include "ren/ren-sdl2.hpp"
#include "Lippincott.hpp"
#include "Renderer.hpp"
#include "Swapchain.hpp"

#include <SDL2/SDL_syswm.h>
#include <SDL2/SDL_vulkan.h>

auto ren::sdl2::create_renderer(unsigned adapter)
    -> expected<std::unique_ptr<IRenderer>> {
  return ren::create_renderer({.adapter = adapter});
}

auto ren::sdl2::get_required_window_flags(IRenderer &) -> Uint32 {
  return SDL_WINDOW_VULKAN;
}

auto ren::sdl2::create_swapchain(IRenderer &irenderer, SDL_Window *window)
    -> expected<std::unique_ptr<ISwapchain>> {
  auto &renderer = static_cast<Renderer &>(irenderer);

  VkSurfaceKHR surface;
  if (!SDL_Vulkan_CreateSurface(window, renderer.get_instance(), &surface)) {
    return std::unexpected(Error::RHI);
  }
  return lippincott([&] {
    auto swapchain = std::make_unique<Swapchain>();
    swapchain->init(
        renderer, surface,
        {
            .get_size = [](void *window) -> glm::uvec2 {
              int w, h;
              SDL_Vulkan_GetDrawableSize((SDL_Window *)window, &w, &h);
              return {w, h};
            },
            .is_fullscreen = [](void *window) -> bool {
              int w, h;
              SDL_GetWindowSize((SDL_Window *)window, &w, &h);
              int display = SDL_GetWindowDisplayIndex((SDL_Window *)window);
              SDL_DisplayMode mode;
              SDL_GetDesktopDisplayMode(display, &mode);
              return mode.w == w and mode.h == h;
            },
            .get_windowing_system = [](void *window) -> WindowingSystem {
              SDL_SysWMinfo info;
              SDL_VERSION(&info.version);
              SDL_GetWindowWMInfo((SDL_Window *)window, &info);
              switch (info.subsystem) {
              default:
                return WindowingSystem::Unknown;
              case SDL_SYSWM_WINDOWS:
                return WindowingSystem::Win32;
              case SDL_SYSWM_X11:
                return WindowingSystem::X11;
              case SDL_SYSWM_WAYLAND:
                return WindowingSystem::Wayland;
              }
            },
        },
        window);
    return swapchain;
  });
}
