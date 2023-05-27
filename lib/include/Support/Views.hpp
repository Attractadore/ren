#pragma once
#include <range/v3/view.hpp>

#include "Optional.hpp"

namespace ren {
constexpr auto concat = ranges::views::concat;
constexpr auto enumerate = ranges::views::enumerate;
constexpr auto filter = ranges::views::filter;
constexpr auto map = ranges::views::transform;
constexpr auto once = ranges::views::single;
constexpr auto zip = ranges::views::zip;

auto filter_map(auto transform_fn) {
  return map(std::move(transform_fn)) |
         filter([]<typename T>(const Optional<T> &opt) { return !!opt; }) |
         map([]<typename T>(const Optional<T> &opt) { return *opt; });
}

template <ranges::sized_range R> size_t size_bytes(R &&r) {
  return ranges::size(r) * sizeof(ranges::range_value_t<R>);
}

template <std::integral I> auto range(I begin, I end) {
  return ranges::views::iota(begin, end);
}

template <std::integral I> auto range(I end) { return range(I(0), end); }
} // namespace ren
