#pragma once
#include "Assert.hpp"
#include "GenIndex.hpp"
#include "Optional.hpp"
#include "TypeTraits.hpp"

#include <algorithm>
#include <concepts>

namespace ren {

template <typename T, std::derived_from<GenIndex> K> class GenMap {
public:
  GenMap() = default;

  GenMap(const GenMap &other) { *this = other; }

  GenMap(GenMap &&other) noexcept { *this = std::move(other); }

  ~GenMap() { destroy(); }

  GenMap &operator=(const GenMap &other)
    requires std::copy_constructible<T>
  {
    [[unlikely]] if (this == &other) { return *this; }

    if constexpr (not std::is_trivially_destructible_v<T>) {
      for (usize i = 0; i < m_capacity; ++i) {
        if (GenIndex::is_active(m_generations[i])) {
          std::destroy_at(&m_values[i]);
        }
      }
    }

    if (other.m_capacity > m_capacity) {
      std::free(m_generations);
      std::free(m_values);
      m_generations = (u8 *)std::malloc(other.m_capacity * sizeof(u8));
      m_values = (T *)std::malloc(other.m_capacity * sizeof(T));
      m_capacity = other.m_capacity;
    }

    std::copy_n(other.m_generations, other.m_capacity, m_generations);
    std::fill(&m_generations[other.m_capacity], &m_generations[m_capacity],
              GenIndex::TOMBSTONE);

    if constexpr (std::is_trivially_copyable_v<T>) {
      std::copy_n(other.m_values, other.m_capacity, m_values);
    } else {
      for (usize i = 0; i < other.m_capacity; ++i) {
        if (GenIndex::is_active(other.m_generations[i])) {
          std::construct_at(&m_values[i], other.m_values[i]);
        }
      }
    }

    m_size = other.m_size;

    return *this;
  }

  GenMap &operator=(GenMap &&other) noexcept {
    destroy();
    m_generations = other.m_generations;
    m_values = other.m_values;
    m_capacity = other.m_capacity;
    m_size = other.m_size;
    other.m_generations = nullptr;
    other.m_values = nullptr;
    other.m_capacity = 0;
    other.m_size = 0;
    return *this;
  }

  auto size() const -> usize { return m_size; }

  auto empty() const { return size() == 0; }

  bool contains(K key) const {
    return key.index < m_capacity and m_generations[key.index] == key.gen and
           GenIndex::is_active(key.gen);
  }

  template <typename Self>
  auto get(this Self &self, K key) -> ConstLikeT<T, Self> & {
    ren_assert(self.contains(key));
    return self.m_values[key];
  }

  template <typename Self>
  auto try_get(this Self &self, K key) -> Optional<ConstLikeT<T, Self> &> {
    if (not self.contains(key)) {
      return None;
    }
    return self.m_values[key];
  }

  template <typename Self>
  auto operator[](this Self &self, K key) -> ConstLikeT<T, Self> & {
    return self.get(key);
  }

  void insert(K key, T value) {
    static_assert(GenIndex::INIT > GenIndex::TOMBSTONE);

    [[unlikely]] if (key >= m_capacity) {
      auto new_capacity = std::max<usize>(m_capacity * 2, key + 1);

      auto *new_generations = (u8 *)std::malloc(new_capacity * sizeof(u8));
      std::copy_n(m_generations, m_capacity, new_generations);
      std::fill(&new_generations[m_capacity], &new_generations[new_capacity],
                GenIndex::TOMBSTONE);

      auto *new_values = (T *)std::malloc(new_capacity * sizeof(T));
      if constexpr (std::is_trivially_copyable_v<T> and
                    std::is_trivially_destructible_v<T>) {
        std::copy_n(m_values, m_capacity, new_values);
      } else {
        for (usize i = 0; i < m_capacity; ++i) {
          if (GenIndex::is_active(m_generations[i])) {
            std::construct_at(&new_values[i], std::move(m_values[i]));
            std::destroy_at(&m_values[i]);
          }
        }
      }

      std::free(m_generations);
      std::free(m_values);
      m_generations = new_generations;
      m_values = new_values;
      m_capacity = new_capacity;
    }

    ren_assert(key.gen >= m_generations[key]);

    if (GenIndex::is_active(m_generations[key])) {
      m_generations[key] = std::max<u8>(m_generations[key], key.gen);
      m_values[key] = std::move(value);
    } else {
      m_generations[key] = key.gen;
      std::construct_at(&m_values[key], std::move(value));
      m_size++;
    }
  }

  void erase(K key) { try_pop(key); }

  auto pop(K key) -> T {
    ren_assert(contains(key));
    m_generations[key]--;
    T value = std::move(m_values[key]);
    std::destroy_at(&m_values[key]);
    m_size--;
    return value;
  }

  auto try_pop(K key) -> Optional<T> {
    if (!contains(key)) {
      return None;
    }
    m_generations[key]--;
    T value = std::move(m_values[key]);
    std::destroy_at(&m_values[key]);
    m_size--;
    return value;
  }

  void clear() {
    for (usize i = 0; i < m_capacity; ++i) {
      if (GenIndex::is_active(m_generations[i])) {
        m_generations[i]--;
        std::destroy_at(&m_values[i]);
      }
    }
    m_size = 0;
  }

private:
  void destroy() {
    if constexpr (not std::is_trivially_destructible_v<T>) {
      for (usize i = 0; i < m_capacity; ++i) {
        if (GenIndex::is_active(m_generations[i])) {
          std::destroy_at(&m_values[i]);
        }
      }
    }
    std::free(m_generations);
    m_generations = nullptr;
    std::free(m_values);
    m_values = nullptr;
    m_capacity = 0;
    m_size = 0;
  }

private:
  u8 *m_generations = nullptr;
  T *m_values = nullptr;
  usize m_capacity = 0;
  usize m_size = 0;
};

} // namespace ren
