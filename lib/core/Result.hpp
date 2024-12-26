#pragma once
#include "Macros.hpp"

#include <expected>

namespace ren {

template <typename T, typename E> using Result = std::expected<T, E>;

template <typename E> using Failure = std::unexpected<E>;

#define ren_try_result ren_cat(res, __LINE__)

#define ren_try(dest, ...)                                                     \
  auto ren_try_result = (__VA_ARGS__);                                         \
  if (!ren_try_result) {                                                       \
    return Failure(std::move(ren_try_result.error()));                         \
  }                                                                            \
  dest = std::move(ren_try_result.value());

#define ren_try_to(...)                                                        \
  if (auto ren_try_result = (__VA_ARGS__); !ren_try_result) {                  \
    return Failure(std::move(ren_try_result.error()));                         \
  }

} // namespace ren
