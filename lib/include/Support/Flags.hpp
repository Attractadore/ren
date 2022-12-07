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

template <ScopedEnum E> constexpr bool EnableFlags = false;

struct EmptyFlagsT {};
} // namespace detail
constexpr detail::EmptyFlagsT EmptyFlags;

template <typename E>
concept FlagsEnum = detail::EnableFlags<E>;

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

#define ENABLE_FLAGS(E)                                                        \
  template <> constexpr inline bool detail::EnableFlags<E> = true;             \
  using E##Flags = Flags<E>

// clang-format off
#define BEGIN_FLAGS_ENUM(E)                                                    \
  namespace detail::E##_impl {                                                 \
    constexpr auto first = __LINE__;                                           \
    enum class E

#define FLAG(flag) flag = 1 << (__LINE__ - first - 1)

#define END_FLAGS_ENUM(E)                                                      \
  ;                                                                            \
  }                                                                            \
  using E = detail::E##_impl::E;                                               \
  ENABLE_FLAGS(E)
// clang-format on
} // namespace ren
