#pragma once
#if _WIN32
#include "ren/core/StdDef.hpp"
#include "ren/core/String.hpp"

#include <Windows.h>
#include <cstdlib>
#include <fmt/base.h>

#define WIN32_CHECK(expr)                                                      \
  do {                                                                         \
    if (!(expr)) {                                                             \
      fmt::println(stderr, #expr " failed: {}", GetLastError());               \
      ren_trap();                                                              \
      exit(EXIT_FAILURE);                                                      \
    }                                                                          \
  } while (0)

#define NTSTATUS_CHECK(expr)                                                   \
  do {                                                                         \
    NTSTATUS status = expr;                                                    \
    if (!status) {                                                             \
      fmt::println(stderr, #expr " failed: {}", status);                       \
      ren_trap();                                                              \
      exit(EXIT_FAILURE);                                                      \
    }                                                                          \
  } while (0)

#define WIN32_CHECK_ERROR(err, message)                                        \
  do {                                                                         \
    if (err) {                                                                 \
      fmt::println(stderr, "{} failed: {}", message, err);                     \
      ren_trap();                                                              \
      exit(EXIT_FAILURE);                                                      \
    }                                                                          \
  } while (0)

namespace ren {

String8 wcs_to_utf8(NotNull<Arena *> arena, const wchar_t *wcs);

String8 wcs_to_utf8(NotNull<Arena *> arena, Span<const wchar_t> wcs);

const wchar_t *utf8_to_path(NotNull<Arena *> arena, String8 str,
                            Span<const wchar_t> suffix = L"");

const wchar_t *utf8_to_raw_path(NotNull<Arena *> arena, String8 str,
                                const wchar_t *suffix = nullptr);

} // namespace ren

#endif
