#pragma once
#include "Errors.hpp"
#include "StdDef.hpp"

#include <span>

namespace ren {

template <typename T, usize Extent = std::dynamic_extent>
struct Span : std::span<T, Extent> {
  using std::span<T, Extent>::span;
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

template <std::contiguous_iterator Iter>
Span(Iter first, usize count)
    -> Span<std::remove_reference_t<std::iter_reference_t<Iter>>>;

template <typename T, usize Extent = std::dynamic_extent>
struct TempSpan : Span<T, Extent> {
  using Span<T, Extent>::Span;
  TempSpan(std::initializer_list<T> ilist)
      : TempSpan(&*ilist.begin(), &*ilist.end()) {}
  TempSpan(T &value)
    requires(not ranges::input_range<T &>)
      : TempSpan(&value, 1) {}
};

} // namespace ren

namespace ranges {

template <typename T, ren::usize Extent>
inline constexpr bool enable_borrowed_range<ren::Span<T, Extent>> = true;

template <typename T, ren::usize Extent>
inline constexpr bool enable_view<ren::Span<T, Extent>> = true;

} // namespace ranges
