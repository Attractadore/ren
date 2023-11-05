#pragma once
#include "Errors.hpp"
#include "StdDef.hpp"

#include <range/v3/iterator.hpp>
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

  auto as_bytes() -> Span<const std::byte> const {
    return {reinterpret_cast<const std::byte *>(this->data()),
            this->size_bytes()};
  }

  template <typename U> auto reinterpret() const {
    using V = std::conditional_t<std::is_const_v<T>, std::add_const_t<U>, U>;
    auto s = Span<V>(reinterpret_cast<V *>(this->data()),
                     this->size_bytes() / sizeof(V));
    assert(s.size_bytes() == this->size_bytes());
    return s;
  }

  auto pop_front() -> T & {
    T &f = this->front();
    *this = this->subspan(1);
    return f;
  }

  auto pop_front(usize count) -> Span {
    Span f = this->first(count);
    *this = this->subspan(count);
    return f;
  }
};

template <ranges::contiguous_range R>
  requires ranges::indirectly_writable<ranges::iterator_t<R>,
                                       ranges::range_value_t<R>>
Span(R &&r) -> Span<ranges::range_value_t<R>>;

template <ranges::contiguous_range R>
Span(R &&r) -> Span<const ranges::range_value_t<R>>;

template <std::contiguous_iterator Iter>
Span(Iter first, usize count)
    -> Span<std::remove_reference_t<std::iter_reference_t<Iter>>>;

template <typename T> struct TempSpan : Span<T> {
  using Span<T>::Span;

  TempSpan(std::initializer_list<T> ilist)
      : TempSpan(&*ilist.begin(), &*ilist.end()) {}

  auto as_bytes() -> TempSpan<const std::byte> const {
    return {reinterpret_cast<const std::byte *>(this->data()),
            this->size_bytes()};
  }
};

template <ranges::contiguous_range R>
TempSpan(R &&r) -> TempSpan<ranges::range_value_t<R>>;

template <std::contiguous_iterator Iter>
TempSpan(Iter first, usize count)
    -> TempSpan<std::remove_reference_t<std::iter_reference_t<Iter>>>;

} // namespace ren

namespace ranges {

template <typename T>
inline constexpr bool enable_borrowed_range<ren::Span<T>> = true;

template <typename T> inline constexpr bool enable_view<ren::Span<T>> = true;

} // namespace ranges
