#include "AppBase.hpp"
#include "ren/core/Chrono.hpp"
#include "ren/core/Format.hpp"

void AppBase::init(ren::String8 app_name) {
  m_arena = ren::Arena::init();
  m_frame_arena = ren::Arena::init();

  m_app_name = app_name.copy(&m_arena);

  unsigned adapter = ren::DEFAULT_ADAPTER;
  const char *user_adapter = std::getenv("REN_ADAPTER");
  if (user_adapter) {
    char *end;
    adapter = std::strtol(user_adapter, &end, 10);
    if (end != user_adapter + std::strlen(user_adapter)) {
      adapter = ren::DEFAULT_ADAPTER;
    }
  }

  m_renderer = ren::create_renderer(&m_arena, {.adapter = adapter});
  if (!m_renderer) {
    exit(EXIT_FAILURE);
  }

  ren::ScratchArena scratch;
  m_window =
      SDL_CreateWindow(app_name.zero_terminated(scratch), 1280, 720,
                       SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE |
                           ren::get_sdl_window_flags(m_renderer));
  if (!m_window) {
    fmt::println(stderr, "{}", SDL_GetError());
    exit(EXIT_FAILURE);
  }

  m_swapchain = ren::create_swapchain(&m_arena, m_renderer, m_window);

  m_scene = ren::create_scene(&m_arena, m_renderer, m_swapchain);
  if (!m_scene) {
    fmt::println(stderr, "Scene initialization failed");
    exit(EXIT_FAILURE);
  }

  m_camera = ren::create_camera(m_scene);
  ren::set_camera(m_scene, m_camera);
}

void AppBase::quit() {
  ren::destroy_scene(m_scene);
  ren::destroy_swap_chain(m_swapchain);
  ren::destroy_renderer(m_renderer);
  SDL_DestroyWindow(m_window);
}

void AppBase::loop() {
  ren::u64 last_time = ren::clock();
  bool quit = false;

  while (!quit) {
    ren::u64 now = ren::clock();
    ren::u64 dt = now - last_time;
    last_time = now;

    float fps = 1e9f / dt;
    auto title = fmt::format("{} @ {:.1f} FPS", m_app_name, fps);
    SDL_SetWindowTitle(m_window, title.c_str());

    ren::delay_input(m_scene);

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT or (e.type == SDL_EVENT_KEY_DOWN and
                                       e.key.scancode == SDL_SCANCODE_ESCAPE)) {
        quit = true;
      }
      process_event(e);
    }

    begin_frame();
    process_frame(dt);
    end_frame();
    ren::draw(m_scene, {.delta_time = dt / 1e9f});
    m_frame_arena.clear();
  }
}

void AppBase::process_event(const SDL_Event &event) {
  if (event.type == SDL_EVENT_KEY_DOWN and
      event.key.scancode == SDL_SCANCODE_F11) {
    bool is_fullscreen = SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN;
    SDL_SetWindowFullscreen(m_window, not is_fullscreen);
  }
}

void AppBase::begin_frame() {}

void AppBase::process_frame(ren::u64 dt_ns) {}

void AppBase::end_frame() {}
