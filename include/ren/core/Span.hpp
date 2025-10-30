#pragma once
#include "Arena.hpp"
#include "Assert.hpp"
#include "NotNull.hpp"
#include "StdDef.hpp"
#include "TypeTraits.hpp"

#include <initializer_list>

namespace ren {

struct Arena;

template <typename T> struct Span {
  T *m_data = nullptr;
  usize m_size = 0;

public:
  constexpr Span() = default;

  constexpr Span(T *ptr, usize size) {
    m_data = ptr;
    m_size = size;
  }

  constexpr Span(T *begin, T *end) {
    ren_assert(begin <= end);
    m_data = begin;
    m_size = end - begin;
  }

  constexpr Span(std::initializer_list<T> elems) {
    m_data = elems.begin();
    m_size = elems.size();
  }

  template <typename U, usize N> constexpr Span(U (&array)[N]) {
    m_data = array;
    m_size = N;
  }

  template <typename R> constexpr Span(R &&r) {
    m_data = r.begin();
    m_size = r.end() - r.begin();
  }

  static const Span<T> allocate(NotNull<Arena *> arena, usize count) {
    return {arena->allocate<T>(count), count};
  }

  constexpr T &operator[](usize i) const {
    ren_assert(i < m_size);
    return m_data[i];
  }

  constexpr T *begin() const { return m_data; }
  constexpr T *end() const { return m_data + m_size; }

  constexpr T &back() const {
    ren_assert(m_size > 0);
    return m_data[m_size - 1];
  }

  operator Span<std::add_const_t<T>>() const { return {m_data, m_size}; }

  usize size_bytes() const { return m_size * sizeof(T); }

  Span<ConstLikeT<std::byte, T>> as_bytes() {
    return {(ConstLikeT<std::byte, T> *)m_data, m_size * sizeof(T)};
  }

  Span<T> subspan(usize start, usize count) const {
    ren_assert(start <= m_size);
    ren_assert(start + count <= m_size);
    return {m_data + start, count};
  }

  Span<T> subspan(usize start) const {
    ren_assert(start <= m_size);
    return {m_data + start, m_size - start};
  }
};

template <typename R>
Span(R &&r) -> Span<std::remove_pointer_t<decltype(std::declval<R>().begin())>>;

template <typename T, usize N> Span(T (&array)[N]) -> Span<T>;

} // namespace ren
