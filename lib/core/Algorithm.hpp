#pragma once
#include <algorithm>

namespace ren {

template <std::ranges::forward_range R>
  requires std::permutable<std::ranges::iterator_t<R>>
auto rotate_left(R &&r) -> std::ranges::borrowed_subrange_t<R> {
  if (std::ranges::empty(r)) {
    return {std::ranges::begin(r), std::ranges::end(r)};
  }
  return std::ranges::rotate(std::forward<R>(r),
                             std::ranges::next(std::ranges::begin(r)));
}

template <std::ranges::random_access_range R>
  requires std::permutable<std::ranges::iterator_t<R>>
auto rotate_right(R &&r) -> std::ranges::borrowed_subrange_t<R> {
  if (std::ranges::empty(r)) {
    return {std::ranges::begin(r), std::ranges::end(r)};
  }
  return std::ranges::rotate(std::forward<R>(r),
                             std::ranges::prev(std::ranges::end(r)));
}

} // namespace ren
