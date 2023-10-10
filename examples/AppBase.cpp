#include "AppBase.hpp"

#include <SDL2/SDL_vulkan.h>
#include <utility>

namespace chrono = std::chrono;

auto get_error_string_impl(std::string err) -> std::string { return err; }

auto get_error_string_impl(ren::Error err) -> std::string {
  switch (err) {
  case ren::Error::Vulkan:
    return "ren: Vulkan error";
  case ren::Error::System:
    return "ren: System error";
  case ren::Error::Runtime:
    return "ren: Runtime error";
  case ren::Error::SDL2:
    return fmt::format("ren: SDL2 error: {}", SDL_GetError());
  case ren::Error::Unknown:
    return "ren: Unknown error";
  }
  std::unreachable();
}

auto throw_error(std::string err) -> std::string {
  throw std::runtime_error(std::move(err));
}

AppBase::AppBase(const char *app_name) {
  [&] -> Result<void> {
    m_window.reset(SDL_CreateWindow(
        app_name, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        m_window_width, m_window_height,
        SDL_WINDOW_RESIZABLE | ren::sdl2::get_window_flags()));
    if (!m_window) {
      bail("{}", SDL_GetError());
    }

    OK(m_swapchain, ren::sdl2::create_swapchain(m_window.get()));

    OK(m_scene, ren::create_scene(m_swapchain));

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
      ren::set_size(m_swapchain, m_window_width, m_window_height);
    }

    TRY_TO(begin_frame());
    TRY_TO(process_frame(m_window_width, m_window_height, dt));
    TRY_TO(ren::draw());
    TRY_TO(end_frame());
  }

  return {};
}

auto AppBase::begin_frame() -> Result<void> { return {}; }

auto AppBase::process_frame(unsigned width, unsigned height,
                            chrono::nanoseconds) -> Result<void> {
  ren::SceneId scene = get_scene();
  ren::set_camera(scene, {.width = width, .height = height});
  return {};
}

auto AppBase::end_frame() -> Result<void> { return {}; }
