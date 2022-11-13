#pragma once
#include "Flags.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <utility>

namespace ren {
namespace detail {
template <typename E> struct FlagsTypeImpl { using type = uint64_t; };

template <FlagsEnum E> struct FlagsTypeImpl<E> { using type = Flags<E>; };

template <typename E> using FlagsType = typename FlagsTypeImpl<E>::type;

template <auto Map>
inline constexpr auto reverse_map = [] {
  constexpr auto N = Map.size();
  using P = typename decltype(Map)::value_type;
  using From = typename P::first_type;
  using To = typename P::second_type;
  std::array<std::pair<To, From>, N> reverse_map;
  for (int i = 0; i < N; ++i) {
    reverse_map[i] = {Map[i].second, Map[i].first};
  }
  return reverse_map;
}();
} // namespace detail

template <typename From, typename To, unsigned N,
          std::array<std::pair<From, To>, N> Map>
constexpr detail::FlagsType<To> remapFlags(detail::FlagsType<From> from_flags) {
  detail::FlagsType<To> to_flags{};
  for (const auto &[ff, tf] : Map) {
    if (int(from_flags & ff)) {
      to_flags |= tf;
    }
  }
  return to_flags;
}

template <typename From, typename To, unsigned N,
          std::array<std::pair<From, To>, N> Map>
constexpr To remapEnum(From from) {
  for (auto [fe, te] : Map) {
    if (from == fe) {
      return te;
    }
  }
  assert(!"Unknown enum value");
  return To();
}

template <auto Map>
inline constexpr auto flagsMap = [] {
  constexpr auto N = Map.size();
  using P = typename decltype(Map)::value_type;
  using From = typename P::first_type;
  using To = typename P::second_type;
  return remapFlags<From, To, N, Map>;
}();

template <auto Map>
inline constexpr auto inverseFlagsMap = [] {
  constexpr auto N = Map.size();
  using P = typename decltype(Map)::value_type;
  using To = typename P::first_type;
  using From = typename P::second_type;
  return remapFlags<From, To, N, detail::reverse_map<Map>>;
}();

template <auto Map>
inline constexpr auto enumMap = [] {
  constexpr auto N = Map.size();
  using P = typename decltype(Map)::value_type;
  using From = typename P::first_type;
  using To = typename P::second_type;
  return remapEnum<From, To, N, Map>;
}();

template <auto Map>
inline constexpr auto inverseEnumMap = [] {
  constexpr auto N = Map.size();
  using P = typename decltype(Map)::value_type;
  using To = typename P::first_type;
  using From = typename P::second_type;
  return remapEnum<From, To, N, detail::reverse_map<Map>>;
}();
} // namespace ren
