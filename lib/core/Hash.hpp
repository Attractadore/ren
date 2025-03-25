#pragma once
#include "StdDef.hpp"

#include <functional>

namespace ren {

template <typename T> struct Hash;

template <typename T>
  requires requires { std::hash<T>(); }
struct Hash<T> : std::hash<T> {};

// boost::hash_combine
template <typename T> auto hash_combine(u64 hash, const T &value) -> u64 {
  hash ^= Hash<T>()(value) + 0x9e3779b9 + (hash << 6U) + (hash >> 2U);
  return hash;
}

} // namespace ren
