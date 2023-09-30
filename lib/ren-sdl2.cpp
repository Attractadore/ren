#include "ren/ren-sdl2.hpp"
#include "Support/Vector.hpp"
#include "ren/ren-vk.hpp"

#include <SDL2/SDL_vulkan.h>

auto ren::sdl2::init(const InitInfo &init_info) -> expected<void> {
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

  return ren::init({
      .instance_extensions = extensions,
      .adapter = init_info.adapter,
  });
}

auto ren::sdl2::get_window_flags() -> Uint32 { return SDL_WINDOW_VULKAN; }

auto ren::sdl2::create_swapchain(SDL_Window *window)
    -> expected<std::unique_ptr<Swapchain>> {
  return ren::vk::Swapchain::create(
      [](VkInstance instance, void *window,
         VkSurfaceKHR *p_surface) -> VkResult {
        if (!SDL_Vulkan_CreateSurface((SDL_Window *)(window), instance,
                                      p_surface)) {
          return VK_ERROR_UNKNOWN;
        }
        return VK_SUCCESS;
      },
      window);
}
