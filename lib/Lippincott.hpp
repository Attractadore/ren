#pragma once
#include "Support/String.hpp"
#include "ren/ren.hpp"

#include <concepts>
#include <functional>
#include <system_error>

namespace ren {

template <std::invocable F>
auto lippincott(F &&f) noexcept -> expected<std::invoke_result_t<F>> {
  try {
    if constexpr (std::is_void_v<std::invoke_result_t<F>>) {
      std::invoke(std::forward<F>(f));
      return {};
    } else {
      return std::invoke(std::forward<F>(f));
    }
  } catch (const std::system_error &) {
    return std::unexpected(Error::System);
  } catch (const std::runtime_error &e) {
    if (StringView(e.what()).starts_with("Vulkan")) {
      return std::unexpected(Error::Vulkan);
    }
    return std::unexpected(Error::Runtime);
  } catch (...) {
    return std::unexpected(Error::Unknown);
  }
}

} // namespace ren
