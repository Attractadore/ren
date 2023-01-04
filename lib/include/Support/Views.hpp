#pragma once
#include <range/v3/range/conversion.hpp>
#include <range/v3/view.hpp>

#include <optional>

namespace ren {
constexpr auto once = ranges::views::single;
constexpr auto concat = ranges::views::concat;
constexpr auto filter = ranges::views::filter;
constexpr auto map = ranges::views::transform;

inline auto filter_map(auto transform_fn) {
  return map(std::move(transform_fn)) |
         filter([]<typename T>(const std::optional<T> &opt) { return !!opt; }) |
         map([]<typename T>(const std::optional<T> &opt) { return *opt; });
}

template <ranges::sized_range R> size_t size_bytes(R &&r) {
  return ranges::size(r) * sizeof(ranges::range_value_t<R>);
}

template <std::integral I> auto range(I end) {
  return ranges::views::iota(I(0), end);
}
} // namespace ren
