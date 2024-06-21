#pragma once
#include "Vector.hpp"

#include <ranges>
#include <utility>

namespace ren {
namespace detail {
template <typename K, typename V, class Compare,
          template <typename...> typename Container>
class LinearMapImpl {
  Container<K> m_keys;
  Container<V> m_values;

private:
  using items_t = decltype(std::views::zip(std::declval<Container<K> &>(),
                                           std::declval<Container<V> &>()));
  using const_items_t =
      decltype(std::views::zip(std::declval<const Container<K> &>(),
                               std::declval<const Container<V> &>()));

  constexpr items_t items() noexcept {
    return std::views::zip(m_keys, m_values);
  }

  constexpr const_items_t items() const noexcept {
    return std::views::zip(m_keys, m_values);
  }

public:
  using key_type = K;
  using value_type = V;
  using iterator = std::ranges::iterator_t<items_t>;
  using const_iterator = std::ranges::iterator_t<const_items_t>;

  constexpr auto keys() noexcept { return items() | std::views::keys; }
  constexpr auto keys() const noexcept { return items() | std::views::keys; }
  constexpr auto values() noexcept { return items() | std::views::values; }
  constexpr auto values() const noexcept {
    return items() | std::views::values;
  }

  constexpr size_t size() const noexcept { return items().size(); }

  constexpr iterator begin() noexcept { return std::ranges::begin(items()); }
  constexpr const_iterator begin() const noexcept {
    return std::ranges::begin(items());
  }
  constexpr const_iterator cbegin() const noexcept {
    return std::ranges::begin(items());
  }

  constexpr iterator end() noexcept { return std::ranges::end(items()); }
  constexpr const_iterator end() const noexcept {
    return std::ranges::end(items());
  }
  constexpr const_iterator cend() const noexcept {
    return std::ranges::end(items());
  }

  constexpr const value_type *data() const noexcept { return m_values.data(); }

  constexpr iterator find(const key_type &key) noexcept {
    return begin() + std::ranges::distance(keys().begin(),
                                           std::ranges::find(keys(), key));
  }

  constexpr const_iterator find(const key_type &key) const noexcept {
    return begin() + std::ranges::distance(keys().begin(),
                                           std::ranges::find(keys(), key));
  }

  constexpr std::pair<iterator, bool> insert(const key_type &key,
                                             const value_type &value) {
    auto it = find(key);
    if (it != end()) {
      return {it, false};
    }
    m_keys.push_back(key);
    m_values.push_back(value);
    return {--end(), true};
  }

  constexpr std::pair<iterator, bool> insert(key_type &&key,
                                             const value_type &value) {
    auto it = find(key);
    if (it != end()) {
      return {it, false};
    }
    m_keys.push_back(std::move(key));
    m_values.push_back(value);
    return {--end(), true};
  }

  constexpr std::pair<iterator, bool> insert(const key_type &key,
                                             value_type &&value) {
    auto it = find(key);
    if (it != end()) {
      return {it, false};
    }
    m_keys.push_back(key);
    m_values.push_back(std::move(value));
    return {--end(), true};
  }

  constexpr std::pair<iterator, bool> insert(key_type &&key,
                                             value_type &&value) {
    auto it = find(key);
    if (it != end()) {
      return {it, false};
    }
    m_keys.push_back(std::move(key));
    m_values.push_back(std::move(value));
    return {--end(), true};
  }

  constexpr value_type &operator[](const key_type &key)
    requires std::default_initializable<value_type>
  {
    auto it = find(key);
    if (it != end()) {
      return std::get<1>(*it);
    }
    m_keys.push_back(key);
    return m_values.emplace_back();
  }

  constexpr value_type &operator[](key_type &&key)
    requires std::default_initializable<value_type>
  {
    auto it = find(key);
    if (it != end()) {
      return std::get<1>(*it);
    }
    m_keys.push_back(std::move(key));
    return m_values.emplace_back();
  }

  constexpr void clear() noexcept {
    m_keys.clear();
    m_values.clear();
  }
};
} // namespace detail

template <typename K, typename V, class Compare = std::ranges::equal_to>
using LinearMap = detail::LinearMapImpl<K, V, Compare, Vector>;

template <typename K, typename V, size_t N,
          class Compare = std::ranges::equal_to>
using SmallLinearMap =
    detail::LinearMapImpl<K, V, Compare,
                          detail::SizedSmallVector<N>::template type>;
} // namespace ren
