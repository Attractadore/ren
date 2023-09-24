#pragma once
#include "StdDef.hpp"

#include <boost/functional/hash.hpp>

#include <functional>

namespace ren {

template <typename T> struct Hash;

template <typename T>
  requires requires { std::hash<T>(); }
struct Hash<T> : std::hash<T> {};

auto hash_combine(usize hash, const auto &value) -> usize {
  boost::hash_combine(hash, value);
  return hash;
}

} // namespace ren
