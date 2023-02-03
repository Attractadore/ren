#pragma once
#include "ren/ren.hpp"

#include <SDL2/SDL.h>

class AppBase {
  struct WindowDeleter {
    void operator()(SDL_Window *window) const noexcept {
      SDL_DestroyWindow(window);
    }
  };

  std::string m_app_name;
  struct SDL {
    SDL() { SDL_Init(SDL_INIT_EVERYTHING); }
    ~SDL() { SDL_Quit(); }
  } SDL;
  std::unique_ptr<SDL_Window, WindowDeleter> m_window;
  ren::UniqueDevice m_device;
  ren::UniqueSwapchain m_swapchain;
  ren::UniqueScene m_scene;

  unsigned m_window_width = 1280, m_window_height = 720;

public:
  AppBase(std::string app_name);

  void run();

protected:
  const ren::Scene &get_scene() const { return *m_scene; }
  ren::Scene &get_scene() { return *m_scene; }

  virtual void process_event(const SDL_Event &e) {}
  virtual void iterate() {}

  std::pair<unsigned, unsigned> get_window_size() const {
    return {m_window_width, m_window_height};
  }

private:
  void loop();
};
