#include "AppBase.hpp"
#include "ren/ren-vk.hpp"

#include <fmt/format.h>

#include <cassert>
#include <utility>

namespace chrono = std::chrono;

namespace {

auto create_swapchain(SDL_Window *window) -> Result<ren::UniqueSwapchain> {
  return ren::vk::Swapchain::create(
             [](VkInstance instance, void *window,
                VkSurfaceKHR *p_surface) -> VkResult {
               if (!SDL_Vulkan_CreateSurface(
                       reinterpret_cast<SDL_Window *>(window), instance,
                       p_surface)) {
                 return VK_ERROR_UNKNOWN;
               }
               return VK_SUCCESS;
             },
             window)
      .transform_error(get_error_string);
}

} // namespace

auto get_error_string_impl(std::string err) -> std::string { return err; }

auto get_error_string_impl(ren::Error err) -> std::string {
  switch (err) {
  case ren::Error::Vulkan:
    return "ren: Vulkan error";
  case ren::Error::System:
    return "ren: System error";
  case ren::Error::Runtime:
    return "ren: Runtime error";
  case ren::Error::Unknown:
    return "ren: Unknown error";
  }
  std::unreachable();
}

auto throw_error(std::string err) -> std::string {
  throw std::runtime_error(std::move(err));
}

AppBase::AppBase(const char *app_name) {
  [&]() -> Result<void> {
    m_window.reset(SDL_CreateWindow(app_name, SDL_WINDOWPOS_UNDEFINED,
                                    SDL_WINDOWPOS_UNDEFINED, m_window_width,
                                    m_window_height,
                                    SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN));
    if (!m_window) {
      bail("{}", SDL_GetError());
    }

    OK(m_swapchain, create_swapchain(m_window.get()));

    OK(m_scene, ren::Scene::create(*m_swapchain));

    return {};
  }()
               .transform_error(throw_error);
}

auto AppBase::loop() -> Result<void> {
  auto last_time = chrono::steady_clock::now();
  bool quit = false;

  while (!quit) {
    auto now = chrono::steady_clock::now();
    chrono::nanoseconds dt = now - last_time;
    last_time = now;

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT or
          (e.type == SDL_KEYDOWN and
           e.key.keysym.scancode == SDL_SCANCODE_ESCAPE)) {
        quit = true;
      }
      TRY_TO(process_event(e));
    }

    {
      int w, h;
      SDL_Vulkan_GetDrawableSize(m_window.get(), &w, &h);
      m_window_width = w;
      m_window_height = h;
      m_swapchain->set_size(m_window_width, m_window_height);
    }

    TRY_TO(iterate(m_window_width, m_window_height, dt));
    TRY_TO(m_scene->draw());
  }

  return {};
}

auto AppBase::get_scene() const -> const ren::Scene & { return *m_scene; }
auto AppBase::get_scene() -> ren::Scene & { return *m_scene; }

auto AppBase::process_event(const SDL_Event &e) -> Result<void> { return {}; }

auto AppBase::iterate(unsigned width, unsigned height, std::chrono::nanoseconds)
    -> Result<void> {
  auto &scene = get_scene();

  TRY_TO(scene.set_camera({
      .width = width,
      .height = height,
  }));

  return {};
}
