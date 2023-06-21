#pragma once
#include "Hash.hpp"
#include "Optional.hpp"

#include <unordered_map>

namespace ren {

template <typename K, typename V>
struct HashMap : std::unordered_map<K, V, Hash<K>> {
  using Base = std::unordered_map<K, V, Hash<K>>;
  using Base::Base;

  using Base::insert;
  auto insert(K key, V value) -> Optional<V &> {
    auto [it, inserted] = insert(std::pair(std::move(key), std::move(value)));
    if (inserted) {
      return it->second;
    }
    return None;
  }

  using Base::operator[];
  auto operator[](const K &key) const -> const V & {
    auto it = this->find(key);
    assert(it != this->end());
    return it->second;
  }

  auto erase_if(const std::predicate<const K &, const V &> auto &pred)
      -> usize {
    return std::erase_if(*this, [&](const std::pair<K, V> &kv) {
      return pred(kv.first, kv.second);
    });
  }
};

} // namespace ren
