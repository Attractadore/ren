#pragma once
#include "Vector.hpp"

#include <range/v3/algorithm.hpp>
#include <range/v3/view.hpp>

#include <utility>

namespace ren {
namespace detail {
template <typename K, typename V, class Compare,
          template <typename...> typename Container>
class LinearMapImpl {
  Container<K> m_keys;
  Container<V> m_values;

private:
  using items_t = decltype(ranges::views::zip(std::declval<Container<K> &>(),
                                              std::declval<Container<V> &>()));
  using const_items_t =
      decltype(ranges::views::zip(std::declval<const Container<K> &>(),
                                  std::declval<const Container<V> &>()));

  constexpr items_t items() noexcept {
    return ranges::views::zip(m_keys, m_values);
  }

  constexpr const_items_t items() const noexcept {
    return ranges::views::zip(m_keys, m_values);
  }

public:
  using key_type = K;
  using value_type = V;
  using iterator = ranges::iterator_t<items_t>;
  using const_iterator = ranges::iterator_t<const_items_t>;

  constexpr auto keys() noexcept { return items() | ranges::views::keys; }
  constexpr auto keys() const noexcept { return items() | ranges::views::keys; }
  constexpr auto values() noexcept { return items() | ranges::views::values; }
  constexpr auto values() const noexcept {
    return items() | ranges::views::values;
  }

  constexpr iterator begin() noexcept { return ranges::begin(items()); }
  constexpr const_iterator begin() const noexcept {
    return ranges::begin(items());
  }
  constexpr const_iterator cbegin() const noexcept {
    return ranges::begin(items());
  }

  constexpr iterator end() noexcept { return ranges::end(items()); }
  constexpr const_iterator end() const noexcept { return ranges::end(items()); }
  constexpr const_iterator cend() const noexcept {
    return ranges::end(items());
  }

  constexpr iterator find(const key_type &key) noexcept {
    return begin() +
           ranges::distance(keys().begin(), ranges::find(keys(), key));
  }

  constexpr const_iterator find(const key_type &key) const noexcept {
    return begin() +
           ranges::distance(keys().begin(), ranges::find(keys(), key));
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
