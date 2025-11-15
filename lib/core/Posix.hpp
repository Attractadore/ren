#pragma once
#if __linux__

#include <cerrno>
#include <cstring>
#include <fmt/base.h>

#define POSIX_CHECK(expr)                                                      \
  do {                                                                         \
    errno = 0;                                                                 \
    (expr);                                                                    \
    if (errno) {                                                               \
      fmt::println(stderr, #expr " failed: {}", std::strerror(errno));         \
      exit(EXIT_FAILURE);                                                      \
    }                                                                          \
  } while (0)

#endif
