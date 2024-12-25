#pragma once
#include "Macros.hpp"

#include <expected>

namespace ren {

template <typename T, typename E> using Result = std::expected<T, E>;

template <typename E> using Failed = std::unexpected<E>;

#define ren_try_result ren_cat(res, __LINE__)

#define ren_try_to(...)                                                        \
  auto ren_try_result = (__VA_ARGS__);                                         \
  static_assert(std::same_as<decltype(ren_try_result)::value_type, void>);     \
  if (!ren_try_result) {                                                       \
    return Failed(std::move(ren_try_result.error()));                          \
  }

} // namespace ren
