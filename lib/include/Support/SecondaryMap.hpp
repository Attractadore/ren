#pragma once
#include "HashMap.hpp"
#include "SlotMapKey.hpp"

namespace ren {

namespace detail {

struct HashIndex {
  template <CSlotMapKey K> auto operator()(K key) const -> u64 {
    return Hash<u32>()(key.slot);
  }
};

struct IndexEqualTo {
  template <CSlotMapKey K> auto operator()(K lhs, K rhs) const -> bool {
    return lhs.slot == rhs.slot;
  }
};

} // namespace detail

template <typename V, CSlotMapKey K = SlotMapKey>
struct SecondaryMap : HashMap<K, V, detail::HashIndex, detail::IndexEqualTo> {
  using Base = HashMap<K, V, detail::HashIndex, detail::IndexEqualTo>;

  using typename Base::const_iterator;
  using typename Base::iterator;

  using Base::Base;

  auto insert(K key, V value) -> Optional<V &> {
    auto it = Base::find(key);
    if (it != this->end()) {
      // Key is already in map
      if (it->first == key) {
        return it->second;
      }
      // Key with mismatched generation is in the map
      Base::erase(it);
    }
    it = Base::try_emplace(it, key, std::move(value));
    assert(it->first == key);
    return None;
  }

  auto insert_or_assign(K key, V value) -> V & {
    auto it = Base::find(key);
    if (it != this->end()) {
      // Key is already in map
      if (it->first == key) {
        it->second = std::move(value);
        return it->second;
      }
      // Key with mismatched generation is in the map
      Base::erase(it);
    }
    it = Base::try_emplace(it, key, std::move(value));
    assert(it->first == key);
    return it->second;
  }

  auto find(K key) const -> const_iterator {
    auto it = Base::find(key);
    if (it != this->end() && it->first == key) {
      return it;
    }
    return this->end();
  }

  auto find(K key) -> iterator {
    auto it = Base::find(key);
    if (it != this->end() && it->first == key) {
      return it;
    }
    return this->end();
  }

  auto contains(K key) const -> bool { return this->find(key) != this->end(); }

  auto operator[](K key) const -> const V & {
    auto it = this->find(key);
    assert(it != this->end());
    return it->second;
  }

  auto operator[](K key) -> V & {
    auto it = this->find(key);
    assert(it != this->end());
    return it->second;
  }
};

} // namespace ren
