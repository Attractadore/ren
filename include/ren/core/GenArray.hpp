#pragma once
#include "Assert.hpp"
#include "GenIndexPool.hpp"
#include "Iterator.hpp"

#include <concepts>
#include <utility>

namespace ren {

template <typename T, std::derived_from<GenIndex> K = Handle<T>>
struct GenArray {
  GenIndexPool<K> m_indices;
  T *m_values = nullptr;

private:
  template <bool> class Iterator;

public:
  static GenArray init(NotNull<Arena *> arena) {
    return {
        .m_indices = GenIndexPool<K>::init(arena),
        .m_values = arena->allocate<T>(),
    };
  }

  using const_iterator = Iterator<true>;
  using iterator = Iterator<false>;

  iterator begin() { return {m_indices.begin(), m_values}; }
  const_iterator begin() const { return {m_indices.begin(), m_values}; }

  iterator end() { return {m_indices.end(), m_values}; }
  const_iterator end() const { return {m_indices.end(), m_values}; }

  T *raw_data()
    requires std::is_trivially_copyable_v<T>
  {
    return m_values;
  }

  const T *raw_data() const
    requires std::is_trivially_copyable_v<T>
  {
    return m_values;
  }

  auto size() const -> usize { return m_indices.size(); }

  auto raw_size() const -> usize { return m_indices.raw_size(); }

  auto empty() const { return size() == 0; }

  bool contains(K key) const { return m_indices.contains(key); }

  T &get(K key) {
    ren_assert(contains(key));
    return m_values[key];
  }

  const T &get(K key) const {
    ren_assert(contains(key));
    return m_values[key];
  }

  T *try_get(K key) {
    if (not contains(key)) {
      return nullptr;
    }
    return &m_values[key];
  }

  const T *try_get(K key) const {
    if (not contains(key)) {
      return nullptr;
    }
    return &m_values[key];
  }

  T &operator[](K key) { return get(key); }
  const T &operator[](K key) const { return get(key); }

  K insert(NotNull<Arena *> arena, T value = {})
    requires std::is_trivially_destructible_v<T>
  {
    usize old_capacity = m_indices.m_generations.m_capacity;
    K key = m_indices.generate(arena);
    usize new_capacity = m_indices.m_generations.m_capacity;

    [[unlikely]] if (new_capacity > old_capacity) {
      if (!arena->expand<T>(m_values, old_capacity, new_capacity)) {
        auto *new_values = arena->allocate<T>(new_capacity);
        std::memcpy(new_values, m_values, sizeof(T) * old_capacity);
        m_values = new_values;
      }
    }
    m_values[key] = value;

    return key;
  }

  void erase(const_iterator it) {
    ren_assert(it != end());
    erase(*it);
  }

  void erase(K key) { pop(key); }

  auto pop(K key) -> T {
    ren_assert(contains(key));
    m_indices.erase(key);
    return m_values[key];
  }

  void clear() { m_indices.clear(); }

private:
  template <bool IsConst>
  using IteratorValueType = std::conditional_t<IsConst, const T, T>;

  template <bool IsConst>
  class Iterator : public IteratorFacade<Iterator<IsConst>> {
  public:
    Iterator() = default;

    using value_type = std::pair<K, T>;

    operator Iterator<true>() const
      requires(not IsConst)
    {
      return {
          .m_it = m_it,
          .m_data = m_data,
      };
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
    template <bool> friend class Iterator;
    friend GenArray;

    Iterator(GenIndexPool<K>::const_iterator it,
             IteratorValueType<IsConst> *data) {
      m_it = it;
      m_data = data;
    }

  private:
    GenIndexPool<K>::const_iterator m_it;
    IteratorValueType<IsConst> *m_data = nullptr;
  };
};

} // namespace ren
