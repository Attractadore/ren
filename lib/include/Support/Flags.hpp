#pragma once
#include <concepts>

namespace ren {
namespace detail {
template <typename E>
concept ScopedEnum = not std::convertible_to<E, std::underlying_type_t<E>>;

enum class TestEnumClass {};
static_assert(ScopedEnum<TestEnumClass>);
enum TestEnum {};
static_assert(not ScopedEnum<TestEnum>);

struct EmptyFlagsT {};
} // namespace detail
inline constexpr detail::EmptyFlagsT EmptyFlags;

template <detail::ScopedEnum E> struct EnableFlags {
  static constexpr bool enable = false;
};

template <typename E>
concept FlagsEnum = EnableFlags<E>::enable;

template <FlagsEnum E> class Flags {
  E m_value = E(0);

public:
  using Enum = E;
  using Underlying = std::underlying_type_t<E>;

  constexpr Flags() noexcept = default;
  constexpr Flags(decltype(EmptyFlags)) noexcept {}
  constexpr Flags(Enum e) noexcept : m_value{e} {}

  constexpr Enum operator&(Enum e) const noexcept {
    return static_cast<Enum>(static_cast<Underlying>(m_value) &
                             static_cast<Underlying>(e));
  }

  constexpr Flags operator&(Flags f) const noexcept {
    return static_cast<Enum>(static_cast<Underlying>(m_value) &
                             static_cast<Underlying>(f.m_value));
  }

  constexpr Flags &operator&=(Flags f) noexcept { return *this = *this & f; }

  constexpr Flags operator|(Flags f) const noexcept {
    return static_cast<Enum>(static_cast<Underlying>(m_value) |
                             static_cast<Underlying>(f.m_value));
  }

  constexpr Flags &operator|=(Flags f) noexcept { return *this = *this | f; }

  constexpr bool isSet(Enum bit) const noexcept {
    return static_cast<Underlying>(*this & bit);
  }

  constexpr bool anySet(Flags mask) const noexcept {
    return (*this & mask) != EmptyFlags;
  }

  constexpr bool allSet(Flags mask) const noexcept {
    return (*this & mask) == mask;
  }

  constexpr bool noneSet(Flags mask) const noexcept {
    return (*this & mask) == EmptyFlags;
  }

  constexpr Enum get() const noexcept { return m_value; }

  constexpr explicit operator bool() const noexcept {
    return static_cast<Underlying>(m_value);
  }

  constexpr bool operator==(const Flags &) const noexcept = default;
};

#define ENABLE_FLAGS(E)                                                        \
  template <> struct EnableFlags<E> { static constexpr bool enable = true; };  \
  using E##Flags = Flags<E>;

template <FlagsEnum E> constexpr E operator&(E l, E r) {
  using U = std::underlying_type_t<E>;
  return static_cast<E>(static_cast<U>(l) & static_cast<U>(r));
}

template <FlagsEnum E> constexpr E operator&(E l, Flags<E> r) { return r & l; }

template <FlagsEnum E> constexpr Flags<E> operator|(E l, E r) {
  return Flags(l) | Flags(r);
}

template <FlagsEnum E> constexpr Flags<E> operator|(E l, Flags<E> r) {
  return r | l;
}
} // namespace ren
