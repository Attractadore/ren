#pragma once
#include "GenIndex.hpp"
#include "Iterator.hpp"
#include "Vector.hpp"
#include "ren/core/Assert.hpp"

#include <concepts>

namespace ren {

template <std::derived_from<GenIndex> K> class GenIndexPool {
public:
  class const_iterator : public IteratorFacade {
  public:
    const_iterator() = default;

    using value_type = K;

  private:
    friend IteratorFacade;
    friend GenIndexPool;

    const_iterator(const u8 *generations, usize size, usize index = 0) {
      m_generations = generations;
      m_size = size;
      m_index = index;
    }

    bool equal(const_iterator other) const {
      return m_generations + m_index == other.m_generations + other.m_index;
    }

    void increment() {
      m_index++;
      while (m_index < m_size) {
        [[likely]] if (GenIndex::is_active(m_generations[m_index])) { break; }
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
    const u8 *m_generations = nullptr;
    usize m_size = 0;
    usize m_index = 0;
  };

  auto begin() const -> const_iterator {
    return {m_generations.data(), m_generations.size()};
  }

  auto end() const -> const_iterator {
    return {m_generations.data(), m_generations.size(), m_generations.size()};
  }

  auto size() const -> usize {
    return m_generations.size() - m_free_list.size();
  }

  auto raw_size() const -> usize { return m_generations.size(); }

  auto empty() const { return size() == 0; }

  bool contains(K key) const {
    return key.index < m_generations.size() and
           m_generations[key.index] == key.gen and GenIndex::is_active(key.gen);
  }

  auto generate() -> K {
    u32 index;
    if (not m_free_list.empty()) {
      index = m_free_list.back();
      m_free_list.pop_back();
      m_generations[index]++;
    } else {
      index = m_generations.size();
      m_generations.emplace_back(GenIndex::INIT);
    }
    ren_assert(GenIndex::is_active(m_generations[index]));
    ren_assert(index < m_generations.size());
    K key;
    key.gen = m_generations[index];
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
    // Deactivate slot once its generation wraps around.
    [[likely]] if (++m_generations[key.index] != GenIndex::TOMBSTONE) {
      m_free_list.push_back(key.index);
    }
  }

  void clear() {
    m_free_list.clear();
    m_generations.clear();
  }

private:
  Vector<u32> m_free_list;
  Vector<u8> m_generations;
};

} // namespace ren
