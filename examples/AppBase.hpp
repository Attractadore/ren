#pragma once
#include "ren/ren.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <chrono>
#include <fmt/format.h>

class AppBase {
  SDL_Window *m_window = nullptr;
  ren::Renderer *m_renderer = nullptr;
  ren::SwapChain *m_swapchain = nullptr;
  ren::Scene *m_scene = nullptr;
  ren::Handle<ren::Camera> m_camera;

  std::string m_app_name;

protected:
  ren::Arena m_arena;
  ren::Arena m_frame_arena;

  void init(const char *app_name);
  void quit();

  auto get_window() const -> SDL_Window * { return m_window; }

  auto get_scene() const -> ren::Scene * { return m_scene; }

  auto get_camera() const -> ren::Handle<ren::Camera> { return m_camera; }

  virtual void process_event(const SDL_Event &e);

  virtual void begin_frame();

  virtual void process_frame(std::chrono::nanoseconds dt);

  virtual void end_frame();

  template <class App, typename... Args> static void run(Args &&...args) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
      fmt::println(stderr, "{}", SDL_GetError());
      exit(EXIT_FAILURE);
    }
    App app;
    app.init(std::forward<Args>(args)...);
    app.loop();
    app.quit();
    SDL_Quit();
  }

private:
  void loop();
};
