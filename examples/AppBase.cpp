#include "AppBase.hpp"

#include <utility>

namespace chrono = std::chrono;

auto get_error_string_impl(std::string err) -> std::string { return err; }

auto get_error_string_impl(ren::Error err) -> std::string {
  switch (err) {
  case ren::Error::RHI:
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
  [&]() -> Result<void> {
    m_app_name = app_name;

    unsigned adapter = ren::DEFAULT_ADAPTER;
    const char *user_adapter = std::getenv("REN_ADAPTER");
    if (user_adapter) {
      char *end;
      adapter = std::strtol(user_adapter, &end, 10);
      if (end != user_adapter + std::strlen(user_adapter)) {
        adapter = ren::DEFAULT_ADAPTER;
      }
    }

    OK(m_renderer, ren::create_renderer({.adapter = adapter}));

    m_window.reset(SDL_CreateWindow(
        app_name, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1280, 720,
        SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE |
            ren::get_sdl_window_flags(*m_renderer)));
    if (!m_window) {
      bail("{}", SDL_GetError());
    }

    OK(m_swapchain, ren::create_swapchain(*m_renderer, m_window.get()));

    OK(m_scene, ren::create_scene(*m_renderer, *m_swapchain));

    OK(m_camera, m_scene->create_camera());
    m_scene->set_camera(m_camera);

    return {};
  }()
               .transform_error(throw_error)
               .value();
}

auto AppBase::loop() -> Result<void> {
  auto last_time = chrono::steady_clock::now();
  bool quit = false;

  while (!quit) {
    auto now = chrono::steady_clock::now();
    chrono::nanoseconds dt = now - last_time;
    last_time = now;

    float fps = 1e9f / dt.count();
    auto title = fmt::format("{} @ {:.1f} FPS", m_app_name, fps);
    SDL_SetWindowTitle(m_window.get(), title.c_str());

    TRY_TO(m_scene->delay_input());

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT or
          (e.type == SDL_KEYDOWN and
           e.key.keysym.scancode == SDL_SCANCODE_ESCAPE)) {
        quit = true;
      }
      TRY_TO(process_event(e));
    }

    TRY_TO(begin_frame());
    TRY_TO(process_frame(dt));
    TRY_TO(end_frame());
    TRY_TO(m_scene->draw());
  }

  return {};
}

auto AppBase::process_event(const SDL_Event &event) -> Result<void> {
  if (event.type == SDL_KEYDOWN and
      event.key.keysym.scancode == SDL_SCANCODE_F11) {
    bool is_fullscreen =
        SDL_GetWindowFlags(m_window.get()) & SDL_WINDOW_FULLSCREEN_DESKTOP;
    SDL_SetWindowFullscreen(m_window.get(),
                            is_fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
  }
  return {};
}

auto AppBase::begin_frame() -> Result<void> { return {}; }

auto AppBase::process_frame(chrono::nanoseconds) -> Result<void> { return {}; }

auto AppBase::end_frame() -> Result<void> { return {}; }
