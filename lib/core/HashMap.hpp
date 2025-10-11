#pragma once
#include "Hash.hpp"
#include "Optional.hpp"
#include "ren/core/Assert.hpp"

#include <unordered_map>

namespace ren {

namespace detail {

template <typename U, typename K, typename KeyHash, typename KeyEqual>
concept CHeterogenousKeyImpl =
    std::invocable<KeyHash, const U &> and
    std::invocable<KeyEqual, const K &, const U &> and
    requires { typename KeyHash::is_transparent; };

} // namespace detail

template <typename K, typename V, typename KeyHash = Hash<K>,
          typename KeyEqual = std::ranges::equal_to>
class HashMap : public std::unordered_map<K, V, KeyHash, KeyEqual> {
  using Base = std::unordered_map<K, V, KeyHash, KeyEqual>;

public:
  using Base::Base;

#define CHeterogenousKey(U)                                                    \
  (not std::same_as<K, U> and                                                  \
   detail::CHeterogenousKeyImpl<U, K, KeyHash, KeyEqual>)

#define CHeterogenousKeyOrKey(U)                                               \
  (std::same_as<K, U> or detail::CHeterogenousKeyImpl<U, K, KeyHash, KeyEqual>)

  void insert(K key, V value) {
    auto [_, inserted] =
        Base::insert(std::pair(std::move(key), std::move(value)));
    ren_assert(inserted);
  }

  auto insert(Base::const_iterator hint, K key, V value) -> Base::iterator {
#if REN_ASSERTIONS
    auto [it, inserted] =
        Base::insert(std::pair(std::move(key), std::move(value)));
    ren_assert(inserted);
    return it;
#else
    auto it = Base::insert(hint, std::pair(std::move(key), std::move(value)));
    return it;
#endif
  }

  using Base::operator[];

  template <typename U>
    requires(CHeterogenousKeyOrKey(U))
  auto operator[](const U &key) const -> const V & {
    auto it = this->find(key);
    ren_assert(it != this->end());
    return it->second;
  }

  template <typename U>
    requires(CHeterogenousKey(U))
  auto operator[](const U &key) -> V & {
    auto it = this->find(key);
    ren_assert(it != this->end());
    return it->second;
  }

  template <typename U>
    requires(CHeterogenousKeyOrKey(U))
  auto get(const U &key) -> Optional<V &> {
    auto it = this->find(key);
    if (it != this->end()) {
      return it->second;
    }
    return None;
  }

  template <typename U>
    requires(CHeterogenousKeyOrKey(U))
  auto get(const U &key) const -> Optional<const V &> {
    auto it = this->find(key);
    if (it != this->end()) {
      return it->second;
    }
    return None;
  }

#undef CHeterogenousKey
#undef CHeterogenousKeyOrKey
};

} // namespace ren
