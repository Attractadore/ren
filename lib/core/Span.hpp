#pragma once
#include "ren/core/StdDef.hpp"

#include <span>

namespace ren {

template <typename T> struct Span : std::span<T, std::dynamic_extent> {
  using std::span<T, std::dynamic_extent>::span;

  template <usize S>
  Span(std::span<T, S> s)
      : std::span<T, std::dynamic_extent>(s.begin(), s.end()) {}

  auto as_bytes() -> Span<const std::byte> const {
    return {(const std::byte *)(this->data()), this->size_bytes()};
  }
};

template <std::ranges::contiguous_range R>
Span(R &&r) -> Span<std::remove_reference_t<std::ranges::range_reference_t<R>>>;

template <std::contiguous_iterator Iter>
Span(Iter first, usize count)
    -> Span<std::remove_reference_t<std::iter_reference_t<Iter>>>;

template <typename T> struct TempSpan : Span<T> {
  using Span<T>::Span;

  TempSpan(std::initializer_list<T> ilist)
      : TempSpan(&*ilist.begin(), &*ilist.end()) {}

  auto as_bytes() -> TempSpan<const std::byte> const {
    return {(const std::byte *)(this->data()), this->size_bytes()};
  }
};

template <std::ranges::contiguous_range R>
TempSpan(R &&r) -> TempSpan<std::ranges::range_value_t<R>>;

template <std::contiguous_iterator Iter>
TempSpan(Iter first, usize count)
    -> TempSpan<std::remove_reference_t<std::iter_reference_t<Iter>>>;

} // namespace ren

namespace std::ranges {

template <typename T>
inline constexpr bool enable_borrowed_range<ren::Span<T>> = true;

template <typename T> inline constexpr bool enable_view<ren::Span<T>> = true;

template <typename T>
inline constexpr bool enable_borrowed_range<ren::TempSpan<T>> = true;

template <typename T>
inline constexpr bool enable_view<ren::TempSpan<T>> = true;

} // namespace std::ranges
