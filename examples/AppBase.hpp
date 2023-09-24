#pragma once
#include "ren/ren.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <fmt/format.h>

#include <chrono>
#include <vector>

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
    void operator()(SDL_Window *window) const noexcept {
      SDL_DestroyWindow(window);
    }
  };

  std::unique_ptr<SDL_Window, WindowDeleter> m_window;
  ren::UniqueSwapchain m_swapchain;
  ren::UniqueScene m_scene;

  unsigned m_window_width = 1280, m_window_height = 720;

protected:
  AppBase(const char *app_name);

  auto get_scene() const -> const ren::Scene &;
  auto get_scene() -> ren::Scene &;

  [[nodiscard]] virtual auto process_event(const SDL_Event &e) -> Result<void>;

  [[nodiscard]] virtual auto iterate(unsigned width, unsigned height,
                                     std::chrono::nanoseconds dt)
      -> Result<void>;

  template <class App, typename... Args>
  [[nodiscard]] static auto run(Args &&...args) -> int {
    auto rc = [&] -> Result<void> {
      if (SDL_Init(SDL_INIT_EVERYTHING)) {
        bail("{}", SDL_GetError());
      }

      if (SDL_Vulkan_LoadLibrary(nullptr)) {
        bail("SDL2: failed to load Vulkan: {}", SDL_GetError());
      }

      uint32_t num_extensions = 0;
      if (!SDL_Vulkan_GetInstanceExtensions(nullptr, &num_extensions,
                                            nullptr)) {
        bail("SDL2: failed to query Vulkan extensions: {}", SDL_GetError());
      }
      std::vector<const char *> extensions(num_extensions);
      if (!SDL_Vulkan_GetInstanceExtensions(nullptr, &num_extensions,
                                            extensions.data())) {
        bail("SDL2: failed to query Vulkan extensions: {}", SDL_GetError());
      }
      TRY_TO(ren::init(extensions).transform_error(get_error_string));

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
