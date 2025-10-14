#pragma once
#include "Array.hpp"
#include "StdDef.hpp"

#include <cstring>

namespace ren {

template <typename C> struct String {
  C *m_str = nullptr;
  usize m_size = 0;

  using Char = std::remove_const_t<C>;

public:
  String() = default;

  String(C *str, usize size) {
    m_str = str;
    m_size = size;
  }

  template <usize N> String(const Char (&cstr)[N]) {
    m_str = cstr;
    m_size = N;
  }

  [[nodiscard]] static String init(const Char *cstr)
    requires std::is_const_v<C>
  {
    return {cstr, std::strlen(cstr)};
  }

  bool starts_with(const Char *str) const {
    for (usize i = 0;; ++i) {
      if (!str[i]) {
        return true;
      }
      if (i == m_size) {
        return false;
      }
      if (m_str[i] != str[i]) {
        return false;
      }
    }
  }

  bool ends_with(const Char *str) const {
    usize len = std::strlen(str);
    for (usize i = 0; i < len; ++i) {
      if (i == m_size) {
        return false;
      }
      if (str[len - i - 1] != m_str[m_size - i - 1]) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] String remove_prefix(usize len) const {
    len = len < m_size ? len : m_size;
    return {m_str + len, m_size - len};
  }

  [[nodiscard]] String remove_suffix(usize len) const {
    len = len < m_size ? len : m_size;
    return {m_str, m_size - len};
  }

  const Char *zero_terminated(NotNull<Arena *> arena) const {
    Char *buf = arena->allocate<Char>(m_size + 1);
    std::memcpy(buf, m_str, m_size * sizeof(Char));
    buf[m_size] = (Char)0;
    return buf;
  }

  String copy(NotNull<Arena *> arena) const {
    Char *buf = arena->allocate<Char>(m_size);
    std::memcpy(buf, m_str, m_size * sizeof(Char));
    return {buf, m_size};
  }

  String find(String<const Char> needle) const {
    for (usize i = 0; i + needle.m_size <= m_size; ++i) {
      bool match = true;
      for (usize k = 0; k < needle.m_size; ++k) {
        if (m_str[i + k] != needle.m_str[k]) {
          match = false;
          break;
        }
      }
      if (match) {
        return {&m_str[i], needle.m_size};
      }
    }
    return {};
  }

  String find(const Char *needle) const {
    usize len = std::strlen(needle);
    return find({needle, len});
  }

  operator String<const Char>() const { return {m_str, m_size}; }
};

template <typename C1, typename C2>
  requires std::same_as<std::remove_const_t<C1>, std::remove_const_t<C2>>
bool operator==(String<C1> lhs, String<C2> rhs) {
  return lhs.m_size == rhs.m_size and
         std::memcmp(lhs.m_str, rhs.m_str, lhs.m_size) == 0;
}

template <typename C1, typename C2>
  requires std::same_as<std::remove_const_t<C1>, C2>
bool operator==(String<C1> lhs, const C2 *rhs) {
  for (usize i = 0;; ++i) {
    if (!rhs[i]) {
      return i == lhs.m_size;
    }
    if (i == lhs.m_size) {
      return false;
    }
    if (lhs.m_str[i] != rhs[i]) {
      return false;
    }
  }
}

template <typename C> struct StringBuilder;

template <typename C> struct StringBuilderInserter {
  StringBuilder<C> *m_builder = nullptr;

public:
  StringBuilderInserter &operator=(C value) {
    m_builder->push(value);
    return *this;
  }
  StringBuilderInserter &operator*() { return *this; }
  StringBuilderInserter &operator++() { return *this; }
  StringBuilderInserter &operator++(int) { return *this; }
};

template <typename C> struct StringBuilder {
  Arena *m_arena = nullptr;
  DynamicArray<C> m_buffer;

public:
  [[nodiscard]] static StringBuilder init(NotNull<Arena *> arena) {
    return {arena};
  }

  [[nodiscard]] static StringBuilder init(NotNull<Arena *> arena,
                                          usize capacity) {
    return {
        .m_arena = arena,
        .m_buffer = DynamicArray<C>::init(arena, capacity),
    };
  }

  String<std::add_const_t<C>> string() const {
    return {m_buffer.m_data, m_buffer.m_size};
  }

  String<C> materialize(NotNull<Arena *> arena) const {
    C *str = arena->allocate<C>(m_buffer.m_size);
    std::memcpy(str, m_buffer.m_data, sizeof(C) * m_buffer.m_size);
    return {str, m_buffer.m_size};
  }

  StringBuilderInserter<C> back_inserter() { return {this}; }

  void reserve(usize capacity) { m_buffer.reserve(m_arena, capacity); };

  void push(C value) { m_buffer.push(m_arena, value); }

  void push(const C *str) {
    while (*str) {
      m_buffer.push(m_arena, *str);
      str++;
    }
  }
};

using String8 = String<const char>;
using StringBuilder8 = StringBuilder<char>;

} // namespace ren
