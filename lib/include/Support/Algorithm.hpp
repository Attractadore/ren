#pragma once
#include <range/v3/algorithm.hpp>

namespace ren {

template <ranges::forward_range R>
  requires ranges::permutable<ranges::iterator_t<R>>
auto rotate_left(R &&r) -> ranges::borrowed_subrange_t<R> {
  return ranges::rotate(std::forward<R>(r), ranges::next(ranges::begin(r)));
}

} // namespace ren
