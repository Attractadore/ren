#pragma once
#include "Array.hpp"
#include "Span.hpp"
#include "StdDef.hpp"

#include <cstring>

namespace ren {

template <typename C> struct String {
  C *m_str = nullptr;
  usize m_size = 0;

public:
  String() = default;

  String(C *str, usize size) {
    m_str = str;
    m_size = size;
  }

  template <usize N> String(const char (&cstr)[N]) {
    m_str = cstr;
    m_size = N - 1;
  }

  [[nodiscard]] static String init(const char *cstr)
    requires std::is_const_v<C>
  {
    return {cstr, std::strlen(cstr)};
  }

  bool starts_with(const char *str) const {
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

  bool ends_with(const char *str) const {
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

  const char *zero_terminated(NotNull<Arena *> arena) const {
    char *buf = arena->allocate<char>(m_size + 1);
    std::memcpy(buf, m_str, m_size);
    buf[m_size] = (char)0;
    return buf;
  }

  String<char> copy(NotNull<Arena *> arena) const {
    char *buf = arena->allocate<char>(m_size);
    std::memcpy(buf, m_str, m_size);
    return {buf, m_size};
  }

  String find(String<const char> needle) const {
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

  String find(const char *needle) const {
    usize len = std::strlen(needle);
    return find({needle, len});
  }

  const char *find(char needle) const {
    for (usize i : range(m_size)) {
      if (m_str[i] == needle) {
        return &m_str[i];
      }
    }
    return nullptr;
  }

  const char *rfind(char needle) const {
    for (isize i = m_size - 1; i >= 0; --i) {
      if (m_str[i] == needle) {
        return &m_str[i];
      }
    }
    return nullptr;
  }

  C *begin() const { return m_str; }

  C *end() const { return m_str + m_size; }

  C &operator[](usize i) const { return m_str[i]; }

  Span<String> split(NotNull<Arena *> arena, char separator) {
    DynamicArray<String> items;

    usize s = 0;
    usize i = 0;
    for (; i < m_size; ++i) {
      if (m_str[i] == separator) {
        items.push(arena, String(&m_str[s], i - s));
        s = i + 1;
      }
    }
    items.push(arena, String(&m_str[s], i - s));

    return {items.m_data, items.m_size};
  }

  Span<String> split(NotNull<Arena *> arena, String<const char> separators) {
    DynamicArray<String> items;

    usize s = 0;
    usize i = 0;
    for (; i < m_size; ++i) {
      bool is_separator = false;
      for (char s : separators) {
        if (m_str[i] == s) {
          is_separator = true;
          break;
        }
      }
      if (is_separator) {
        items.push(arena, String(&m_str[s], i - s));
        s = i + 1;
      }
    }
    items.push(arena, String(&m_str[s], i - s));

    return {items.m_data, items.m_size};
  }

  operator String<const char>() const { return {m_str, m_size}; }

  explicit operator bool() const { return m_size > 0; }

  String<C> substr(usize start, usize count) const {
    ren_assert(start + count <= m_size);
    return {m_str + start, count};
  }
};

using String8 = String<const char>;

inline bool operator==(String8 lhs, String8 rhs) {
  return lhs.m_size == rhs.m_size and
         std::memcmp(lhs.m_str, rhs.m_str, lhs.m_size) == 0;
}

inline bool operator==(String8 lhs, const char *rhs) {
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

struct StringBuilder;

struct StringBuilderInserter {
  StringBuilder *m_builder = nullptr;

public:
  StringBuilderInserter &operator=(char value);
  StringBuilderInserter &operator*() { return *this; }
  StringBuilderInserter &operator++() { return *this; }
  StringBuilderInserter &operator++(int) { return *this; }
};

struct StringBuilder {
  Arena *m_arena = nullptr;
  DynamicArray<char> m_buffer;

public:
  [[nodiscard]] static StringBuilder init(NotNull<Arena *> arena) {
    return {arena};
  }

  [[nodiscard]] static StringBuilder init(NotNull<Arena *> arena,
                                          usize capacity) {
    return {
        .m_arena = arena,
        .m_buffer = DynamicArray<char>::init(arena, capacity),
    };
  }

  String8 string() const { return {m_buffer.m_data, m_buffer.m_size}; }

  String8 materialize(NotNull<Arena *> arena) const {
    char *str = arena->allocate<char>(m_buffer.m_size);
    std::memcpy(str, m_buffer.m_data, m_buffer.m_size);
    return {str, m_buffer.m_size};
  }

  StringBuilderInserter back_inserter() { return {this}; }

  void reserve(usize capacity) { m_buffer.reserve(m_arena, capacity); };

  void push(char c) { m_buffer.push(m_arena, c); }

  void push(const char *str) {
    usize len = std::strlen(str);
    m_buffer.push(m_arena, str, len);
  }

  void push(String8 str) { m_buffer.push(m_arena, str.m_str, str.m_size); }

  char pop() { return m_buffer.pop(); }

  void join(Span<String8> strs, String8 separator) {
    [[unlikely]] if (strs.empty()) { return; }
    push(strs[0]);
    for (usize i : range<usize>(1, strs.size())) {
      push(separator);
      push(strs[i]);
    }
  }
};

using StringBuilder8 = StringBuilder;

inline StringBuilderInserter &StringBuilderInserter::operator=(char c) {
  m_builder->push(c);
  return *this;
}

} // namespace ren
