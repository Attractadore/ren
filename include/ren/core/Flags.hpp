#pragma once
#include <type_traits>

namespace ren {

template <typename E>
  requires std::is_enum_v<E> and (not std::is_convertible_v<E, int>)
constexpr bool ENABLE_ENUM_FLAGS = false;

struct EmptyFlagsT {};

inline constexpr EmptyFlagsT EmptyFlags;

template <typename E>
concept CFlagsEnum = ENABLE_ENUM_FLAGS<E>;

template <CFlagsEnum E> class Flags {
  E m_value = E(0);

public:
  using Enum = E;
  using Underlying = std::underlying_type_t<E>;

  constexpr Flags() noexcept = default;
  constexpr Flags(EmptyFlagsT) noexcept {}
  constexpr Flags(Enum e) noexcept : m_value{e} {}

  constexpr Enum operator&(Enum bit) const noexcept {
    return static_cast<Enum>(static_cast<Underlying>(m_value) &
                             static_cast<Underlying>(bit));
  }

  constexpr Flags operator&(Flags mask) const noexcept {
    return static_cast<Enum>(static_cast<Underlying>(m_value) &
                             static_cast<Underlying>(mask.m_value));
  }

  constexpr Flags &operator&=(Flags mask) noexcept {
    return *this = *this & mask;
  }

  constexpr Flags operator|(Flags mask) const noexcept {
    return static_cast<Enum>(static_cast<Underlying>(m_value) |
                             static_cast<Underlying>(mask.m_value));
  }

  constexpr Flags &operator|=(Flags mask) noexcept {
    return *this = *this | mask;
  }

  constexpr Flags &reset(Flags mask) noexcept {
    m_value = static_cast<Enum>(static_cast<Underlying>(m_value) &
                                ~static_cast<Underlying>(mask.m_value));
    return *this;
  }

  constexpr bool is_set(Enum bit) const noexcept {
    return static_cast<Underlying>(*this & bit);
  }

  constexpr bool is_any_set(Flags mask) const noexcept {
    return (*this & mask) != EmptyFlags;
  }

  constexpr bool is_all_set(Flags mask) const noexcept {
    return (*this & mask) == mask;
  }

  constexpr bool is_none_set(Flags mask) const noexcept {
    return (*this & mask) == EmptyFlags;
  }

  constexpr bool is_subset(Flags mask) const noexcept {
    auto intersect = *this & mask;
    return intersect and (*this) == intersect;
  }

  constexpr Enum get() const noexcept { return m_value; }

  constexpr explicit operator bool() const noexcept {
    return static_cast<Underlying>(m_value);
  }

  constexpr bool operator==(const Flags &) const noexcept = default;
};

template <CFlagsEnum E> constexpr E operator&(E l, E r) {
  using U = std::underlying_type_t<E>;
  return static_cast<E>(static_cast<U>(l) & static_cast<U>(r));
}

template <CFlagsEnum E> constexpr E operator&(E l, Flags<E> r) { return r & l; }

template <CFlagsEnum E> constexpr Flags<E> operator|(E l, E r) {
  return Flags(l) | Flags(r);
}

template <CFlagsEnum E> constexpr Flags<E> operator|(E l, Flags<E> r) {
  return r | l;
}

#define REN_BEGIN_FLAGS_ENUM(E)                                                \
  namespace detail::E##Impl {                                                  \
    constexpr auto FIRST = __LINE__;                                           \
    enum class E

#define REN_FLAG(Flag) Flag = 1 << (__LINE__ - FIRST - 1)

#define REN_END_FLAGS_ENUM(E)                                                  \
  ;                                                                            \
  }                                                                            \
  using E = detail::E##Impl::E;

#define REN_ENABLE_FLAGS(E)                                                    \
  template <> constexpr inline bool ::ren::ENABLE_ENUM_FLAGS<E> = true

} // namespace ren
