#pragma once
#include "Optional.hpp"
#include "SlotMapKey.hpp"

#include <range/v3/view.hpp>

namespace ren {

template <typename T, CSlotMapKey K, template <typename> typename C>
class DenseSlotMap {
  static constexpr auto NULL_SLOT = (1 << K::index_bits) - 1;

  struct Slot {
    union {
      uint32_t index : K::index_bits = 0;
      uint32_t next_free : K::index_bits;
    };
    uint32_t version : K::version_bits = 0;
  };

  using Keys = C<K>;
  using Values = C<T>;
  using Slots = C<Slot>;

  Keys m_keys;
  Values m_values;
  Slots m_slots;

  struct FreeHead {
    uint32_t value = NULL_SLOT;
    FreeHead() = default;
    FreeHead(const FreeHead &other) = default;
    FreeHead(FreeHead &other) noexcept
        : value(std::exchange(other.value, NULL_SLOT)) {}
    FreeHead &operator=(const FreeHead &other) = default;
    FreeHead &operator=(FreeHead &other) noexcept {
      value = other.value;
      other.value = NULL_SLOT;
      return *this;
    }
    FreeHead &operator=(uint32_t new_value) noexcept {
      value = new_value;
      return *this;
    }
    operator uint32_t() const noexcept { return value; }
  } m_free_head;

public:
  using key_type = K;
  using value_type = T;
  using const_iterator = ranges::iterator_t<decltype(ranges::views::zip(
      std::declval<const decltype(m_keys) &>(),
      std::declval<const decltype(m_values) &>()))>;
  using iterator = ranges::iterator_t<decltype(ranges::views::zip(
      std::declval<decltype(m_keys) &>(),
      std::declval<decltype(m_values) &>()))>;
  using const_reference = typename const_iterator::reference;
  using reference = typename iterator::reference;
  using difference_type = std::iter_difference_t<iterator>;
  using size_type = std::make_unsigned_t<difference_type>;

  constexpr auto keys() const noexcept { return ranges::views::all(m_keys); }

  constexpr auto values() const noexcept {
    return ranges::views::all(m_values);
  }

  constexpr auto values() noexcept { return ranges::views::all(m_values); }

  constexpr const_iterator cbegin() const noexcept { return begin(); }

  constexpr const_iterator cend() const noexcept { return end(); }

  constexpr const_iterator begin() const noexcept {
    return ranges::views::zip(m_keys, m_values).begin();
  }

  constexpr const_iterator end() const noexcept {
    return ranges::views::zip(m_keys, m_values).end();
  }

  constexpr iterator begin() noexcept {
    return ranges::views::zip(m_keys, m_values).begin();
  }

  constexpr iterator end() noexcept {
    return ranges::views::zip(m_keys, m_values).end();
  }

  constexpr bool empty() const noexcept { return begin() == end(); }

  static constexpr size_type max_size() noexcept { return NULL_SLOT - 1; }

  constexpr size_type size() const noexcept {
    return static_cast<size_type>(ranges::distance(begin(), end()));
  }

  constexpr difference_type ssize() const noexcept {
    return ranges::distance(begin(), end());
  }

  constexpr const_reference front() const noexcept {
    assert(not empty());
    return *begin();
  }

  constexpr reference front() noexcept {
    assert(not empty());
    return *begin();
  }

  constexpr const_reference back() const noexcept {
    assert(not empty());
    return *--end();
  }

  constexpr reference back() noexcept {
    assert(not empty());
    return *--end();
  }

  constexpr void reserve(size_type capacity)
    requires requires(size_type capacity) {
               m_keys.reserve(capacity);
               m_values.reserve(capacity);
               m_slots.reserve(capacity);
             }
  {
    m_keys.reserve(capacity);
    m_values.reserve(capacity);
    m_slots.reserve(capacity);
  }

  constexpr size_type capacity() const noexcept
    requires requires {
               { m_keys.capacity() } -> std::convertible_to<size_type>;
               { m_values.capacity() } -> std::convertible_to<size_type>;
               { m_slots.capacity() } -> std::convertible_to<size_type>;
             }
  {
    return std::min({
        static_cast<size_type>(m_keys.capacity()),
        static_cast<size_type>(m_values.capacity()),
        static_cast<size_type>(m_slots.capacity()),
    });
  }

  constexpr void shrink_to_fit() noexcept
    requires requires {
               m_keys.shrink_to_fit();
               m_values.shrink_to_fit();
               m_slots.shrink_to_fit();
             }
  {
    m_keys.shrink_to_fit();
    m_values.shrink_to_fit();
    m_slots.shrink_to_fit();
  }

  constexpr void clear() noexcept {
    // Push all objects into free list to preserve version info
    for (auto [slot, version] : m_keys) {
      m_slots[slot].next_free = m_free_head;
      m_free_head = slot;
      m_slots[slot].version = version + 1;
    }
    m_keys.clear();
    m_values.clear();
  }

  [[nodiscard]] constexpr key_type insert(const value_type &value)
    requires std::copy_constructible<value_type>
  {
    return emplace(value);
  }

  [[nodiscard]] constexpr key_type insert(value_type &&val)
    requires std::move_constructible<value_type>
  {
    return emplace(std::move(val));
  }

  template <typename... Args>
    requires std::constructible_from<value_type, Args &&...>
  [[nodiscard]] constexpr key_type emplace(Args &&...args) {
    uint32_t index = m_keys.size();
    auto key = [&] {
      if (m_free_head == NULL_SLOT) {
        assert(m_keys.size() == m_slots.size());
        auto slot_index = index;
        auto &slot = m_slots.emplace_back();
        slot.index = index;
        return key_type(slot_index);
      } else {
        auto slot_index = m_free_head;
        auto &slot = m_slots[slot_index];
        m_free_head = slot.next_free;
        slot.index = index;
        return key_type(slot_index, slot.version);
      }
    }();
    m_keys.push_back(key);
    m_values.emplace_back(std::forward<Args>(args)...);
    return key;
  }

  constexpr iterator erase(iterator it) noexcept {
    auto index = ranges::distance(begin(), it);
    erase(index);
    return ranges::next(begin(), index);
  }

  constexpr void erase(key_type k) noexcept { erase(index(k)); }

  [[nodiscard]] constexpr bool try_erase(key_type k) noexcept {
    auto it = find(k);
    if (it != end()) {
      erase(it);
      return true;
    }
    return false;
  }

  [[nodiscard]] constexpr value_type pop(key_type k) noexcept {
    auto erase_index = index(k);
    auto temp = std::exchange(m_values[erase_index], m_values.back());
    m_values.pop_back();
    erase_only_key(erase_index);
    return temp;
  }

  [[nodiscard]] constexpr Optional<value_type> try_pop(key_type key) noexcept {
    auto it = find(key);
    if (it != end()) {
      auto erase_index = ranges::distance(begin(), it);
      auto temp = std::exchange(m_values[erase_index], m_values.back());
      m_values.pop_back();
      erase_only_key(erase_index);
      return std::move(temp);
    }
    return None;
  }

  constexpr void swap(DenseSlotMap &other) noexcept {
    ranges::swap(m_keys, other.m_keys);
    ranges::swap(m_values, other.m_values);
    ranges::swap(m_slots, other.m_slots);
    ranges::swap(m_free_head, other.m_free_head);
  }

  constexpr const_iterator find(key_type k) const noexcept {
    assert(k.slot < m_slots.size());
    auto &slot = m_slots[k.slot];
    if (slot.version == k.version) {
      return ranges::next(begin(), slot.index);
    }
    return end();
  }

  constexpr iterator find(key_type k) noexcept {
    assert(k.slot < m_slots.size());
    auto &slot = m_slots[k.slot];
    if (slot.version == k.version) {
      return ranges::next(begin(), slot.index);
    }
    return end();
  }

  constexpr const value_type *get(key_type key) const noexcept {
    auto it = find(key);
    return it != end() ? &std::get<1>(*it) : nullptr;
  }

  constexpr value_type *get(key_type key) noexcept {
    auto it = find(key);
    return it != end() ? &std::get<1>(*it) : nullptr;
  }

  constexpr const value_type &operator[](key_type k) const noexcept {
    return m_values[index(k)];
  }

  constexpr value_type &operator[](key_type k) noexcept {
    return m_values[index(k)];
  }

  constexpr bool contains(key_type k) const noexcept {
    return find(k) != end();
  };

  constexpr bool operator==(const DenseSlotMap &other) const noexcept {
    return this->m_keys == other.m_keys and this->m_values == other.m_values;
  }

private:
  constexpr uint32_t index(key_type k) const noexcept {
    assert(k.slot < m_slots.size());
    auto slot = m_slots[k.slot];
    assert(slot.index < m_values.size());
    return slot.index;
  }

  constexpr void erase(uint32_t index) noexcept {
    assert(index < size());
    // Erase object from object array
    ranges::swap(m_values[index], m_values.back());
    m_values.pop_back();
    erase_only_key(index);
  }

  constexpr void erase_only_key(uint32_t index) noexcept {
    auto back_key = m_keys.back();
    auto erase_key = std::exchange(m_keys[index], back_key);
    m_keys.pop_back();
    // Order important for back_key = erase_key
    auto &back_slot = m_slots[back_key.slot];
    auto &erase_slot = m_slots[erase_key.slot];
    back_slot.index = index;
    erase_slot.next_free = m_free_head;
    m_free_head = erase_key.slot;
    erase_slot.version = erase_key.version + 1;
  }
};

template <typename T, CSlotMapKey K, template <typename> typename C>
constexpr void swap(DenseSlotMap<T, K, C> &l,
                    DenseSlotMap<T, K, C> &r) noexcept {
  l.swap(r);
}

} // namespace ren