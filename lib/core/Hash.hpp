#pragma once
#include "StdDef.hpp"

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/variadic/to_seq.hpp>

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

#define REN_HASH_COMBINE_FIELD(r, data, elem)                                  \
  seed = hash_combine(seed, data.elem);

#define REN_DEFINE_TYPE_HASH(Type, ...)                                        \
  template <> struct Hash<Type> {                                              \
    auto operator()(const Type &value) const noexcept -> u64 {                 \
      u64 seed = 0;                                                            \
      BOOST_PP_SEQ_FOR_EACH(REN_HASH_COMBINE_FIELD, value,                     \
                            BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))             \
      return seed;                                                             \
    }                                                                          \
  }

} // namespace ren
