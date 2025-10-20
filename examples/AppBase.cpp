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
  case ren::Error::SDL:
    return fmt::format("ren: SDL error: {}", SDL_GetError());
  case ren::Error::Unknown:
    return "ren: Unknown error";
  }
  std::unreachable();
}

auto throw_error(std::string err) -> std::string {
  throw std::runtime_error(std::move(err));
}

auto AppBase::init(const char *app_name) -> Result<void> {
  m_app_name = app_name;

  m_arena = ren::Arena::init();
  m_frame_arena = ren::Arena::init();

  unsigned adapter = ren::DEFAULT_ADAPTER;
  const char *user_adapter = std::getenv("REN_ADAPTER");
  if (user_adapter) {
    char *end;
    adapter = std::strtol(user_adapter, &end, 10);
    if (end != user_adapter + std::strlen(user_adapter)) {
      adapter = ren::DEFAULT_ADAPTER;
    }
  }

  OK(m_renderer, ren::create_renderer(&m_arena, {.adapter = adapter}));

  m_window =
      SDL_CreateWindow(app_name, 1280, 720,
                       SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE |
                           ren::get_sdl_window_flags(m_renderer));
  if (!m_window) {
    bail("{}", SDL_GetError());
  }

  OK(m_swapchain, ren::create_swapchain(&m_arena, m_renderer, m_window));

  OK(m_scene, ren::create_scene(&m_arena, m_renderer, m_swapchain));

  m_camera = ren::create_camera(m_scene);
  ren::set_camera(m_scene, m_camera);

  return {};
}

AppBase::~AppBase() {
  ren::destroy_scene(m_scene);
  ren::destroy_swap_chain(m_swapchain);
  ren::destroy_renderer(m_renderer);
  SDL_DestroyWindow(m_window);
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
    SDL_SetWindowTitle(m_window, title.c_str());

    TRY_TO(ren::delay_input(m_scene));

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT or (e.type == SDL_EVENT_KEY_DOWN and
                                       e.key.scancode == SDL_SCANCODE_ESCAPE)) {
        quit = true;
      }
      TRY_TO(process_event(e));
    }

    TRY_TO(begin_frame());
    TRY_TO(process_frame(dt));
    TRY_TO(end_frame());
    TRY_TO(ren::draw(m_scene, {.delta_time = dt.count() / 1e9f}));
    m_frame_arena.clear();
  }

  return {};
}

auto AppBase::process_event(const SDL_Event &event) -> Result<void> {
  if (event.type == SDL_EVENT_KEY_DOWN and
      event.key.scancode == SDL_SCANCODE_F11) {
    bool is_fullscreen = SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN;
    SDL_SetWindowFullscreen(m_window, not is_fullscreen);
  }
  return {};
}

auto AppBase::begin_frame() -> Result<void> { return {}; }

auto AppBase::process_frame(chrono::nanoseconds) -> Result<void> { return {}; }

auto AppBase::end_frame() -> Result<void> { return {}; }
