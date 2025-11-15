#pragma once
#if _WIN32
#include <Windows.h>
#include <cstdlib>
#include <fmt/base.h>

#define WIN32_CHECK(expr)                                                      \
  do {                                                                         \
    if (!(expr)) {                                                             \
      fmt::println(stderr, #expr " failed: {}", GetLastError());               \
      exit(EXIT_FAILURE);                                                      \
    }                                                                          \
  } while (0)

#define WIN32_CHECK_ERROR(err, message)                                        \
  do {                                                                         \
    if (err) {                                                                 \
      fmt::println(stderr, "{} failed: {}", message, err);                     \
      exit(EXIT_FAILURE);                                                      \
    }                                                                          \
  } while (0)

#endif
