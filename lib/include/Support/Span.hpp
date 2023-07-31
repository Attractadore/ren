#pragma once
#include "Errors.hpp"
#include "StdDef.hpp"

#include <span>

namespace ren {

template <typename T> struct Span : std::span<T, std::dynamic_extent> {
  Span() : Span(nullptr, usize(0)) {}

  Span(T *ptr, usize count)
      : std::span<T, std::dynamic_extent>::span(ptr, count) {}

  Span(T *first, T *last)
      : std::span<T, std::dynamic_extent>::span(first, last) {}

  template <typename R>
    requires(not std::same_as<std::remove_cvref_t<R>, Span<T>>) and
            std::ranges::contiguous_range<R>
  Span(R &&r) : Span(ranges::data(r), ranges::size(r)) {}

  Span(T &value)
    requires(not ranges::input_range<T &>)
      : Span(&value, 1) {}

  auto as_bytes() -> Span<const std::byte> const {
    return {reinterpret_cast<const std::byte *>(this->data()),
            this->size_bytes()};
  }

  template <typename U> auto reinterpret() const -> Span<U> {
    auto s = Span<U>(reinterpret_cast<U *>(this->data()),
                     this->size_bytes() / sizeof(U));
    assert(s.size_bytes() == this->size_bytes());
    return s;
  }
};

template <ranges::contiguous_range R>
Span(R &&r) -> Span<ranges::range_value_t<R>>;

template <std::contiguous_iterator Iter>
Span(Iter first, usize count)
    -> Span<std::remove_reference_t<std::iter_reference_t<Iter>>>;

template <typename T> struct TempSpan : Span<T> {
  using Span<T>::Span;

  TempSpan(std::initializer_list<T> ilist)
      : TempSpan(&*ilist.begin(), &*ilist.end()) {}

  TempSpan(T &&value)
    requires(not ranges::input_range<T &>)
      : TempSpan(&value, 1) {}

  auto as_bytes() -> TempSpan<const std::byte> const {
    return {reinterpret_cast<const std::byte *>(this->data()),
            this->size_bytes()};
  }
};

} // namespace ren

namespace ranges {

template <typename T>
inline constexpr bool enable_borrowed_range<ren::Span<T>> = true;

template <typename T> inline constexpr bool enable_view<ren::Span<T>> = true;

} // namespace ranges
