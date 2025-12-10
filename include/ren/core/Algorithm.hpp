#pragma once
#include "Assert.hpp"
#include "Span.hpp"

#include <cstring>
#include <initializer_list>

namespace ren {

template <typename T> constexpr T max(T lhs, T rhs) {
  return lhs > rhs ? lhs : rhs;
}

template <typename T> constexpr T max(std::initializer_list<T> elems) {
  ren_assert(elems.size() > 0);
  auto it = elems.begin();
  auto end = elems.end();
  T m = *(it++);
  for (; it != end; ++it) {
    m = max(m, *it);
  }
  return m;
}

template <typename T> T min(T lhs, T rhs) { return lhs < rhs ? lhs : rhs; }

template <typename T> T min(std::initializer_list<T> elems) {
  ren_assert(elems.size() > 0);
  auto it = elems.begin();
  auto end = elems.end();
  T m = *(it++);
  for (; it != end; ++it) {
    m = min(m, *it);
  }
  return m;
}

template <typename T, typename U> void copy(Span<T> from, U *to) {
  for (usize i : range(from.m_size)) {
    to[i] = from[i];
  }
}

template <typename T> void copy(Span<const T> from, T *to) {
  ren_assert(to + from.m_size <= from.m_data or
             from.m_data + from.m_size <= to);
  std::memcpy(to, from.m_data, from.size_bytes());
}

template <typename T> void copy(Span<T> from, T *to) {
  return copy(Span<const T>(from), to);
}

template <typename T, typename U> void copy(T *begin, T *end, U *to) {
  ren_assert(begin <= end);
  return copy(Span<T>(begin, end), to);
}

template <typename T, typename U> void copy(T *begin, usize count, U *to) {
  return copy(Span<T>(begin, count), to);
}

template <typename T> void copy_overlapped(Span<const T> from, T *to) {
  ::memmove(to, from.m_data, from.size_bytes());
}

template <typename T> void copy_overlapped(Span<T> from, T *to) {
  ::memmove(to, from.m_data, from.size_bytes());
}

template <typename T> void copy_overlapped(const T *from, usize count, T *to) {
  return copy_overlapped(Span(from, count), to);
}

template <typename T> void copy_overlapped(T *from, usize count, T *to) {
  return copy_overlapped(Span(from, count), to);
}

template <typename T, typename U, typename V>
void exclusive_scan(Span<T> input, U *output, V acc) {
  for (usize i : range(input.m_size)) {
    output[i] = acc;
    acc += input[i];
  }
}

template <typename T, typename U> T *find(Span<T> r, U value) {
  for (T &elem : r) {
    if (elem == value) {
      return &elem;
    }
  }
  return nullptr;
}

template <typename T, typename F> T *find_if(Span<T> r, F condition) {
  for (T &elem : r) {
    if (condition(elem)) {
      return &elem;
    }
  }
  return nullptr;
}

template <typename T, typename U> constexpr void fill(Span<T> r, U value) {
  for (T &elem : r) {
    elem = value;
  }
}

template <typename T, typename U>
constexpr void fill(T *begin, usize count, U value) {
  return fill(Span(begin, count), value);
}

} // namespace ren
