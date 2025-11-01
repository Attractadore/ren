#pragma once
#include "Array.hpp"
#include "Assert.hpp"
#include "GenIndex.hpp"
#include "Iterator.hpp"

#include <concepts>

namespace ren {

template <std::derived_from<GenIndex> K> struct GenIndexPool {
  constexpr static u32 FREE_LIST_END = (1 << 24) - 1;
  constexpr static u32 ACTIVE = 0;

  DynamicArray<GenIndex> m_generations;
  u32 m_free_list = FREE_LIST_END;
  u32 m_num_free = 0;

public:
  class const_iterator : public IteratorFacade {
  public:
    const_iterator() = default;

    using value_type = K;

    bool equal(const_iterator other) const {
      return m_generations + m_index == other.m_generations + other.m_index;
    }

    void increment() {
      m_index++;
      while (m_index < m_size) {
        [[likely]] if (m_generations[m_index].index == ACTIVE) { break; }
        m_index++;
      }
    }

    auto dereference() const -> K {
      ren_assert(m_index < m_size);
      K key;
      key.gen = m_generations[m_index];
      key.index = m_index;
      return key;
    }

  private:
    friend GenIndexPool;

    const_iterator(const GenIndex *generations, usize size, usize index = 0) {
      m_generations = generations;
      m_size = size;
      m_index = index;
    }

  private:
    const GenIndex *m_generations = nullptr;
    usize m_size = 0;
    usize m_index = 0;
  };

  static GenIndexPool init(NotNull<Arena *> arena) {
    GenIndexPool pool;
    pool.m_generations.push(arena, {0, FREE_LIST_END});
    return pool;
  }

  auto begin() const -> const_iterator {
    return {m_generations.m_data, m_generations.m_size, 1};
  }

  auto end() const -> const_iterator {
    return {m_generations.m_data, m_generations.m_size, m_generations.m_size};
  }

  auto size() const -> usize { return m_generations.m_size - 1 - m_num_free; }

  auto raw_size() const -> usize { return m_generations.m_size; }

  auto empty() const { return size() == 0; }

  bool contains(K key) const {
    return key.index < m_generations.m_size and
           m_generations[key.index].gen == key.gen and
           m_generations[key.index].index == ACTIVE;
  }

  auto generate(NotNull<Arena *> arena) -> K {
    u32 index;
    if (m_num_free > 0) {
      index = m_free_list;
      m_free_list = m_generations[index].index;
      m_generations[index].index = ACTIVE;
      m_num_free--;
    } else {
      index = m_generations.m_size;
      m_generations.push(arena, {0, ACTIVE});
    }
    ren_assert(index < m_generations.m_size);
    K key;
    key.gen = m_generations[index].gen;
    key.index = index;
    return key;
  }

  void erase(const_iterator it) {
    ren_assert(it != end());
    erase(*it);
  }

  void erase(K key) {
    if (not contains(key)) {
      return;
    }
    [[likely]] if (++m_generations[key.index].gen != 0) {
      m_generations[key.index].index = m_free_list;
      m_free_list = key.index;
      m_num_free++;
    } else {
      // Deactivate slot once its generation wraps around.
      m_generations[key.index].index = FREE_LIST_END;
    }
  }

  void clear() {
    m_generations.m_size = 1;
    m_free_list = FREE_LIST_END;
    m_num_free = 0;
  }
};

} // namespace ren
