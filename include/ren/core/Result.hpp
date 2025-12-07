#pragma once
#include "Assert.hpp"

#include <concepts>
#include <type_traits>

namespace ren {

struct ResultIgnorer;

template <typename T, typename E>
  requires std::is_trivially_destructible_v<E>
class Result {
  union {
    T m_value;
    E m_error;
  };
  bool m_has_value = false;
  bool m_was_checked = false;

public:
  Result(T value)
    requires std::is_trivially_destructible_v<T>
  {
    m_value = value;
    m_has_value = true;
  }

  Result(std::convertible_to<E> auto error) { m_error = error; }

  Result(const Result &other) = delete;

  Result(Result &&other) {
    if (other.m_has_value) {
      m_value = other.m_value;
      m_has_value = true;
    } else {
      m_error = other.m_error;
      m_has_value = false;
    }
    m_was_checked = other.m_was_checked;
    other.m_was_checked = true;
  }

  Result &operator=(const Result &other) = delete;

  Result &operator=(Result &&other) {
    ren_assert(m_was_checked);
    if (other.m_has_value) {
      m_value = other.m_value;
      m_has_value = true;
    } else {
      m_error = other.m_error;
      m_has_value = false;
    }
    m_was_checked = other.m_was_checked;
    other.m_was_checked = true;
    return *this;
  }

  Result &operator=(T value) {
    ren_assert(m_was_checked);
    m_value = value;
    m_has_value = true;
    m_was_checked = true;
    return *this;
  }

  ~Result() { ren_assert(m_was_checked); }

  explicit operator bool() {
    m_was_checked = true;
    return m_has_value;
  }

  T *operator->() {
    ren_assert(m_has_value);
    ren_assert(m_was_checked);
    return &m_value;
  }

  T &operator*() {
    ren_assert(m_has_value);
    ren_assert(m_was_checked);
    return m_value;
  }

  T value_or(T default_value) {
    m_was_checked = true;
    return m_has_value ? m_value : default_value;
  }

  E error() const {
    ren_assert(not m_has_value);
    ren_assert(m_was_checked);
    return m_error;
  }
};

template <typename E>
  requires std::is_trivially_destructible_v<E>
class Result<void, E> {
  E m_error;
  bool m_has_value = true;
  bool m_was_checked = true;
  friend ResultIgnorer;

public:
  Result() = default;

  Result(std::convertible_to<E> auto error) {
    m_error = error;
    m_has_value = false;
    m_was_checked = false;
  }

  Result(const Result &other) = delete;

  Result(Result &&other) {
    if (other.m_has_value) {
      m_has_value = true;
    } else {
      m_error = other.m_error;
      m_has_value = false;
    }
    m_was_checked = other.m_was_checked;
    other.m_was_checked = true;
  }

  Result &operator=(const Result &other) = delete;

  Result &operator=(Result &&other) {
    ren_assert(m_was_checked);
    if (other.m_has_value) {
      m_has_value = true;
    } else {
      m_error = other.m_error;
      m_has_value = false;
    }
    m_was_checked = other.m_was_checked;
    other.m_was_checked = true;
    return *this;
  }

  ~Result() { ren_assert(m_was_checked); }

  explicit operator bool() {
    m_was_checked = true;
    return m_has_value;
  }

  E error() const {
    ren_assert(not m_has_value);
    ren_assert(m_was_checked);
    return m_error;
  }
};

template <typename T, typename E>
constexpr bool IsTriviallyDestructible<Result<T, E>> = true;

inline struct ResultIgnorer {
  template <typename E> ResultIgnorer &operator=(Result<void, E> &&result) {
    result.m_was_checked = true;
    return *this;
  }
} IgnoreResult;

enum class IoError {
  Unknown,
  Access,
  NotFound,
  Exists,
  Fragmented,
};

template <typename T> using IoResult = Result<T, IoError>;

} // namespace ren
