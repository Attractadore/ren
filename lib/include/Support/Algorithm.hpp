#pragma once
#include <range/v3/algorithm.hpp>

namespace ren {

template <ranges::forward_range R>
  requires ranges::permutable<ranges::iterator_t<R>>
auto rotate_left(R &&r) -> ranges::borrowed_subrange_t<R> {
  return ranges::rotate(std::forward<R>(r), ranges::next(ranges::begin(r)));
}

template <ranges::random_access_range R>
  requires ranges::permutable<ranges::iterator_t<R>>
auto rotate_right(R &&r) -> ranges::borrowed_subrange_t<R> {
  if (not ranges::empty(r)) {
    return ranges::rotate(std::forward<R>(r), ranges::prev(ranges::end(r)));
  }
  return {ranges::end(r), ranges::end(r)};
}

} // namespace ren
