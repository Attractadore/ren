#pragma once
#include "Errors.hpp"
#include "Hash.hpp"
#include "Optional.hpp"

#include <unordered_map>

namespace ren {

namespace detail {

template <typename U, typename K, typename KeyHash, typename KeyEqual>
concept CHeterogenousKeyImpl =
    std::invocable<KeyHash, const U &> and
    std::invocable<KeyEqual, const K &, const U &> and
    requires { typename KeyHash::is_transparent; };
}

template <typename K, typename V, typename KeyHash = Hash<K>,
          typename KeyEqual = std::equal_to<>>
struct HashMap : std::unordered_map<K, V, KeyHash, KeyEqual> {
  using Base = std::unordered_map<K, V, KeyHash, KeyEqual>;
  using Base::Base;

#define CHeterogenousKey(U)                                                    \
  (not std::same_as<K, U> and                                                  \
   detail::CHeterogenousKeyImpl<U, K, KeyHash, KeyEqual>)

#define CHeterogenousKeyOrKey(U)                                               \
  (std::same_as<K, U> or detail::CHeterogenousKeyImpl<U, K, KeyHash, KeyEqual>)

  using Base::insert;
  void insert(K key, V value) {
    auto [_, inserted] = insert(std::pair(std::move(key), std::move(value)));
    assert(inserted);
  }

  auto insert(Base::const_iterator hint, K key, V value) -> Base::iterator {
#if REN_ASSERTIONS
    auto [it, inserted] = insert(std::pair(std::move(key), std::move(value)));
    assert(inserted);
    return it;
#else
    auto it = insert(hint, std::pair(std::move(key), std::move(value)));
    return it;
#endif
  }

  using Base::operator[];

  template <typename U>
    requires CHeterogenousKeyOrKey(U)
  auto operator[](const U &key) const -> const V & {
    auto it = this->find(key);
    assert(it != this->end());
    return it->second;
  }

  template <typename U>
    requires CHeterogenousKey(U)
  auto operator[](const U &key) -> V & {
    auto it = this->find(key);
    assert(it != this->end());
    return it->second;
  }

  template <typename U>
    requires CHeterogenousKeyOrKey(U)
  auto get(const U &key) -> Optional<V &> {
    auto it = this->find(key);
    if (it != this->end()) {
      return it->second;
    }
    return None;
  }

  template <typename U>
    requires CHeterogenousKeyOrKey(U)
  auto get(const U &key) const -> Optional<const V &> {
    auto it = this->find(key);
    if (it != this->end()) {
      return it->second;
    }
    return None;
  }

  auto erase_if(const std::predicate<const K &, const V &> auto &pred)
      -> usize {
    return std::erase_if(*this, [&](const std::pair<K, V> &kv) {
      return pred(kv.first, kv.second);
    });
  }

#undef CHeterogenousKey
#undef CHeterogenousKeyOrKey
};

} // namespace ren
