#pragma once

namespace ren {

template <typename S> S SuccessStatus = S();

template <typename T, typename S> struct Result2 {
  T m_value = T();
  S m_status = SuccessStatus<S>;

public:
  Result2() = default;
  Result2(T value) { m_value = value; }
  Result2(S status) { m_status = status; }
  Result2(T value, S status) {
    m_value = value;
    m_status = status;
  }

  explicit operator bool() const { return m_status == SuccessStatus<S>; }
};

} // namespace ren
