#pragma once
#include "String.hpp"

#include <fmt/format.h>

namespace ren {

template <typename C>
fmt::basic_string_view<std::remove_const_t<C>> format_as(String<C> str) {
  return {str.m_str, str.m_size};
}

template <typename C>
fmt::basic_string_view<C> format_as(StringBuilder<C> str) {
  return {str.m_buffer.m_data, str.m_buffer.m_size};
}

template <typename... T>
[[nodiscard]] String8 format(NotNull<Arena *> arena,
                             fmt::format_string<T...> fmt, T &&...args) {
  auto builder = StringBuilder8::init(arena);
  fmt::vformat_to(builder.back_inserter(), fmt.str,
                  fmt::vargs<T...>{{args...}});
  return builder.string();
}

} // namespace ren
