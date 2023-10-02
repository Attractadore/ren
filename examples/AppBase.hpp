#pragma once
#include "ren/ren-sdl2.hpp"

#include <SDL2/SDL.h>
#include <chrono>
#include <fmt/format.h>

template <typename T = void> using Result = std::expected<T, std::string>;
using Err = std::unexpected<std::string>;

auto get_error_string_impl(std::string err) -> std::string;

auto get_error_string_impl(ren::Error err) -> std::string;

constexpr inline struct {
  auto operator()(auto err) const -> std::string {
    return get_error_string_impl(std::move(err));
  }
} get_error_string;

[[noreturn]] auto throw_error(std::string err) -> std::string;

#define CAT_IMPL(x, y) x##y
#define CAT(x, y) CAT_IMPL(x, y)

#define TRY_RESULT CAT(res, __LINE__)

#define bail(msg, ...) return Err(fmt::format(msg __VA_OPT__(, ) __VA_ARGS__))

#define OK(name, ...)                                                          \
  auto TRY_RESULT = (__VA_ARGS__).transform_error(get_error_string);           \
  if (!TRY_RESULT) {                                                           \
    bail("{}", std::move(TRY_RESULT).error());                                 \
  }                                                                            \
  name = *std::move(TRY_RESULT)

#define TRY_TO(...)                                                            \
  auto TRY_RESULT = (__VA_ARGS__).transform_error(get_error_string);           \
  static_assert(std::same_as<decltype(TRY_RESULT)::value_type, void>);         \
  if (!TRY_RESULT) {                                                           \
    bail("{}", std::move(TRY_RESULT).error());                                 \
  }

class AppBase {
  struct WindowDeleter {
    static void operator()(SDL_Window *window) noexcept {
      SDL_DestroyWindow(window);
    }
  };

  std::unique_ptr<SDL_Window, WindowDeleter> m_window;
  ren::Swapchain m_swapchain;
  ren::Scene m_scene;

  unsigned m_window_width = 1280, m_window_height = 720;

protected:
  AppBase(const char *app_name);

  auto get_window() const -> SDL_Window * { return m_window.get(); }

  auto get_scene() const -> ren::SceneId { return m_scene; }

  [[nodiscard]] virtual auto process_event(const SDL_Event &e) -> Result<void> {
    return {};
  }

  [[nodiscard]] virtual auto iterate(unsigned width, unsigned height,
                                     std::chrono::nanoseconds dt)

      -> Result<void>;

  template <class App, typename... Args>
  [[nodiscard]] static auto run(Args &&...args) -> int {
    auto rc = [&] -> Result<void> {
      if (SDL_Init(SDL_INIT_EVERYTHING)) {
        bail("{}", SDL_GetError());
      }

      TRY_TO(ren::sdl2::init().transform_error(get_error_string));

      return [&] -> Result<App> {
        try {
          return App(std::forward<Args>(args)...);
        } catch (std::exception &err) {
          bail("{}", err.what());
        }
      }()
                        .and_then(&AppBase::loop);
    }()
                         .transform_error([](std::string_view err) {
                           fmt::println(stderr, "{}", err);
                           return -1;
                         })
                         .error_or(0);
    ren::quit();
    SDL_Quit();
    return rc;
  }

private:
  [[nodiscard]] auto loop() -> Result<void>;
};
