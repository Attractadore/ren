#pragma once
#include "Assert.hpp"

#include <concepts>
#include <type_traits>

namespace ren {

template <typename P>
  requires std::is_pointer_v<P>
class NotNull {
public:
  NotNull(std::nullptr_t) = delete;

  template <std::convertible_to<P> A> NotNull(A ptr) {
    ren_assert(ptr);
    m_ptr = ptr;
  }

  auto &operator*() const { return *m_ptr; }

  auto operator->() const -> P { return m_ptr; }

  operator P() const { return m_ptr; }

  auto get() const -> P { return m_ptr; }

private:
  P m_ptr = nullptr;
};

} // namespace ren
