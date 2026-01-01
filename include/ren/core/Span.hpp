#pragma once
#include "Arena.hpp"
#include "Assert.hpp"
#include "NotNull.hpp"
#include "StdDef.hpp"

#include <cstring>
#include <initializer_list>

namespace ren {

struct Arena;

namespace detail {

template <typename R>
using RangeValueType =
    std::remove_pointer_t<decltype(std::declval<R>().begin())>;

} // namespace detail

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

  constexpr Span(std::initializer_list<T> elems)
    requires std::is_const_v<T>
  {
    m_data = elems.begin();
    m_size = elems.size();
  }

  template <typename U, usize N> constexpr Span(U (&array)[N]) {
    m_data = array;
    m_size = N;
  }

  template <typename R>
  constexpr Span(R &&r)
    requires std::same_as<detail::RangeValueType<R>, T> or
             (std::is_const_v<T> and
              std::same_as<const detail::RangeValueType<R>, T>)
  {
    m_data = r.begin();
    m_size = r.end() - r.begin();
  }

  static const Span<T> allocate(NotNull<Arena *> arena, usize count) {
    return Span(arena->allocate<T>(count), count);
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

  auto as_bytes() const {
    using Byte =
        std::conditional_t<std::is_const_v<T>, const std::byte, std::byte>;
    return Span<Byte>((Byte *)m_data, m_size * sizeof(T));
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

  Span<std::remove_const_t<T>> copy(NotNull<Arena *> arena) const {
    auto *data = arena->allocate<std::remove_const_t<T>>(m_size);
    std::memcpy(data, m_data, m_size * sizeof(T));
    return {data, m_size};
  }

  usize size() const { return m_size; }

  bool is_empty() const { return size() == 0; }

  T *data() const { return m_data; }
};

template <typename R> Span(R &&r) -> Span<detail::RangeValueType<R>>;

template <typename T, usize N> Span(T (&array)[N]) -> Span<T>;

template <typename T> Span(std::initializer_list<T>) -> Span<const T>;

} // namespace ren
