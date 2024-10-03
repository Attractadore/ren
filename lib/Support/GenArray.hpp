#pragma once
#include "Assert.hpp"
#include "GenIndexPool.hpp"
#include "Optional.hpp"
#include "TypeTraits.hpp"

#include <algorithm>
#include <boost/iterator/iterator_facade.hpp>
#include <concepts>

namespace ren {

template <typename T, std::derived_from<GenIndex> K = Handle<T>>
class GenArray {
public:
  GenArray() = default;

  GenArray(const GenArray &other) { *this = other; }

  GenArray(GenArray &&other) noexcept { *this = std::move(other); }

  ~GenArray() { destroy(); }

  GenArray &operator=(const GenArray &other)
    requires std::copy_constructible<T>
  {
    [[unlikely]] if (this == &other) { return *this; }

    clear();

    m_indices = other.m_indices;

    if (other.m_capacity > m_capacity) {
      std::free(m_values);
      m_values = std::malloc(other.m_capacity * sizeof(T));
      m_capacity = other.m_capacity;
    }

    if constexpr (std::is_trivially_copyable_v<T>) {
      std::copy_n(other.m_values, other.m_capacity, m_values);
    } else {
      for (K key : m_indices) {
        std::construct_at(&m_values[key], other.m_values[key]);
      }
    }

    return *this;
  }

  GenArray &operator=(GenArray &&other) noexcept {
    destroy();
    m_indices = std::move(other.m_indices);
    m_values = other.m_values;
    m_capacity = other.m_capacity;
    other.m_values = nullptr;
    other.m_capacity = 0;
    return *this;
  }

private:
  template <bool> class Iterator;

public:
  using const_iterator = Iterator<true>;
  using iterator = Iterator<false>;

  template <typename Self>
  auto begin(this Self &self) -> Iterator<std::is_const_v<Self>> {
    return {self.m_indices.begin(), &self.m_values[0]};
  }

  template <typename Self>
  auto end(this Self &self) -> Iterator<std::is_const_v<Self>> {
    return {self.m_indices.end(), &self.m_values[0]};
  }

  auto size() const -> usize { return m_indices.size(); }

  auto empty() const { return size() == 0; }

  bool contains(K key) const { return m_indices.contains(key); }

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

  auto insert(T value) -> K {
    K new_key = m_indices.generate();

    [[unlikely]] if (new_key >= m_capacity) {
      auto new_capacity = std::max<usize>(m_capacity * 2, new_key + 1);

      auto *new_values = (T *)std::malloc(new_capacity * sizeof(T));
      if constexpr (std::is_trivially_copyable_v<T>) {
        std::copy_n(m_values, m_capacity, new_values);
      } else {
        for (K key : m_indices) {
          [[unlikely]] if (key == new_key) { continue; }
          std::construct_at(&new_values[key], std::move(m_values[key]));
          std::destroy_at(&m_values[key]);
        }
      }

      std::free(m_values);
      m_values = new_values;
      m_capacity = new_capacity;
    }

    std::construct_at(&m_values[new_key], std::move(value));

    return new_key;
  }

  template <typename... Ts>
    requires std::constructible_from<T, Ts &&...>
  auto emplace(Ts &&...args) -> K {
    return insert(T(std::forward<Ts>(args)...));
  }

  void erase(const_iterator it) {
    ren_assert(it != end());
    erase(*it);
  }

  void erase(K key) { try_pop(key); }

  auto pop(K key) -> T {
    ren_assert(contains(key));
    m_indices.erase(key);
    T value = std::move(m_values[key]);
    std::destroy_at(&m_values[key]);
    return value;
  }

  auto try_pop(K key) -> Optional<T> {
    if (not contains(key)) {
      return None;
    }
    m_indices.erase(key);
    T value = std::move(m_values[key]);
    std::destroy_at(&m_values[key]);
    return value;
  }

  void clear() {
    if constexpr (not std::is_trivially_destructible_v<T>) {
      for (K key : m_indices) {
        std::destroy_at(&m_values[key]);
      }
    }
    m_indices.clear();
  }

private:
  void destroy() {
    clear();
    std::free(m_values);
    m_values = nullptr;
    m_capacity = 0;
  }

private:
  template <bool IsConst>
  using IteratorValueType = std::conditional_t<IsConst, const T, T>;

  template <bool IsConst>
  class Iterator : public boost::iterator_facade<
                       Iterator<IsConst>, std::pair<const K, T>,
                       boost::forward_traversal_tag,
                       std::pair<const K, IteratorValueType<IsConst> &>> {
  public:
    Iterator() = default;

    operator Iterator<true>() const
      requires(not IsConst)
    {
      return {
          .m_it = m_it,
          .m_data = m_data,
      };
    }

  private:
    template <bool> friend class Iterator;
    friend boost::iterator_core_access;
    friend GenArray;

    Iterator(GenIndexPool<K>::const_iterator it,
             IteratorValueType<IsConst> *data) {
      m_it = it;
      m_data = data;
    }

    template <bool IsOtherConst>
    bool equal(Iterator<IsOtherConst> other) const {
      return m_it == other.m_it;
    }

    void increment() { ++m_it; }

    auto dereference() const -> std::pair<K, IteratorValueType<IsConst> &> {
      K key = *m_it;
      return {key, m_data[key]};
    }

  private:
    GenIndexPool<K>::const_iterator m_it;
    IteratorValueType<IsConst> *m_data = nullptr;
  };

  GenIndexPool<K> m_indices;
  T *m_values = nullptr;
  usize m_capacity = 0;
};

} // namespace ren
