#pragma once
#include "ren/ren.hpp"

#include <SDL2/SDL.h>

#include <cassert>
#include <charconv>
#include <iostream>
#include <vector>

inline std::string env(const char *var) {
  auto *val = std::getenv(var);
  return val ? val : "";
}

struct Renderer {
  virtual ~Renderer() = default;

  virtual Uint32 get_SDL2_flags() const = 0;

  virtual void create_instance() = 0;

  virtual bool select_adapter(unsigned idx) = 0;
  virtual std::string get_adapter_name() const = 0;

  virtual ren::UniqueDevice create_device() = 0;

  virtual ren::UniqueSwapchain create_swapchain(ren::Device &device,
                                                SDL_Window *window) = 0;
};

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
  std::unique_ptr<Renderer> m_renderer;
  ren::UniqueDevice m_device;
  ren::UniqueSwapchain m_swapchain;
  ren::UniqueScene m_scene;

  unsigned m_window_width = 1280, m_window_height = 720;

public:
  AppBase(std::string app_name);
  void run();

protected:
  virtual void process_event(const SDL_Event &e) {}
  virtual void iterate() {}

  const ren::Scene &get_scene() const { return *m_scene; }
  ren::Scene &get_scene() { return *m_scene; }

  std::pair<unsigned, unsigned> get_window_size() const {
    return {m_window_width, m_window_height};
  }

  float get_window_aspect_ratio() const {
    auto [width, height] = get_window_size();
    return float(width) / float(height);
  }

private:
  void select_renderer();
  void loop();
};

inline void AppBase::run() {
  bool quit = false;
  while (!quit) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) {
        quit = true;
      }
      process_event(e);
    }

    {
      int w, h;
      SDL_GetWindowSize(m_window.get(), &w, &h);
      m_window_width = w;
      m_window_height = h;
    }
    m_scene->set_output_size(m_window_width, m_window_height);
    m_swapchain->set_size(m_window_width, m_window_height);

    iterate();
    m_scene->draw();
  }
  std::cout << "Done\n";
}

inline AppBase::AppBase(std::string app_name)
    : m_app_name(std::move(app_name)) {
  select_renderer();

  std::cout << "Create SDL_Window\n";
  m_window.reset(
      SDL_CreateWindow(m_app_name.c_str(), SDL_WINDOWPOS_UNDEFINED,
                       SDL_WINDOWPOS_UNDEFINED, m_window_width, m_window_height,
                       SDL_WINDOW_RESIZABLE | m_renderer->get_SDL2_flags()));

  m_renderer->create_instance();

  std::string ren_adapter = env("REN_ADAPTER");
  unsigned adapter_idx = 0;
  auto [end, ec] = std::from_chars(
      ren_adapter.data(), ren_adapter.data() + ren_adapter.size(), adapter_idx);
  if (end != ren_adapter.data() + ren_adapter.size() or
      ec != std::error_code()) {
    adapter_idx = 0;
  };
  if (!m_renderer->select_adapter(adapter_idx)) {
    throw std::runtime_error{"Failed to find adapter"};
  }
  std::cout << "Running on " << m_renderer->get_adapter_name() << "\n";

  m_device = m_renderer->create_device();

  m_swapchain = m_renderer->create_swapchain(*m_device.get(), m_window.get());

  std::cout << "Create ren::Scene\n";
  m_scene = m_device->create_scene();
  m_scene->set_swapchain(m_swapchain.get());
}
