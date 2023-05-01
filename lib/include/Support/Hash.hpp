#pragma once
#include <functional>

namespace ren {

template <typename T> struct Hash;

template <typename T>
  requires requires { std::hash<T>(); }
struct Hash<T> : std::hash<T> {};

} // namespace ren
