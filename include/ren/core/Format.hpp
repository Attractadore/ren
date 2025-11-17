#pragma once
#include "Result.hpp"
#include "String.hpp"

#include <fmt/format.h>

namespace ren {

inline fmt::basic_string_view<char> format_as(String8 str) {
  return {str.m_str, str.m_size};
}

inline fmt::basic_string_view<char> format_as(StringBuilder str) {
  return {str.m_buffer.m_data, str.m_buffer.m_size};
}

template <typename... T>
[[nodiscard]] String8 format(NotNull<Arena *> arena,
                             fmt::format_string<T...> fmt, T &&...args) {
  ScratchArena scratch;
  auto builder = StringBuilder8::init(scratch);
  fmt::vformat_to(builder.back_inserter(), fmt.str,
                  fmt::vargs<T...>{{args...}});
  return builder.materialize(arena);
}

template <typename... T>
void format_to(NotNull<StringBuilder8 *> builder, fmt::format_string<T...> fmt,
               T &&...args) {
  fmt::vformat_to(builder->back_inserter(), fmt.str,
                  fmt::vargs<T...>{{args...}});
}

String8 format_as(IoError status);

} // namespace ren
