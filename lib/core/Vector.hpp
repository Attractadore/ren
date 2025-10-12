#pragma once
#include "ren/core/StdDef.hpp"

#include <algorithm>
#include <boost/container/small_vector.hpp>
#include <iterator>
#include <vector>

namespace ren {

namespace detail {

template <typename Base> struct VectorExtension : Base {
  using Base::Base;

  template <std::ranges::input_range R> VectorExtension(R &&range) {
    this->assign(std::forward<R>(range));
  }

  auto append(std::ranges::input_range auto &&r) {
    return this->append(std::ranges::begin(r), std::ranges::end(r));
  }

  template <std::input_iterator I, std::sentinel_for<I> S>
  auto append(I i, S s) {
    if constexpr (std::same_as<I, S>) {
      return this->insert(this->end(), i, s);
    } else {
      using CI = std::common_iterator<I, S>;
      return this->insert(this->end(), CI(i), CI(s));
    }
  }

  using Base::assign;

  void assign(std::ranges::input_range auto &&r) {
    this->assign(std::ranges::begin(r), std::ranges::end(r));
  }

  template <std::input_iterator I, std::sentinel_for<I> S>
    requires(not std::same_as<I, S>)
  void assign(I first, S last) {
    using C = std::common_iterator<I, S>;
    this->assign(C(first), C(last));
  }

  using Base::erase;

  template <std::equality_comparable_with<typename Base::value_type> U>
  auto erase(const U &value) -> usize {
    auto r = std::ranges::remove(*this, value);
    usize count = std::ranges::size(r);
    this->erase(std::ranges::begin(r), std::ranges::end(r));
    return count;
  }

  template <std::predicate<typename Base::value_type> F>
  auto erase_if(F &&filter) -> usize {
    auto r = std::ranges::remove_if(*this, std::forward<F>(filter));
    usize count = std::ranges::size(r);
    this->erase(std::ranges::begin(r), std::ranges::end(r));
    return count;
  }
};

} // namespace detail

template <typename T> using Vector = detail::VectorExtension<std::vector<T>>;

template <typename T, size_t N = 64 / sizeof(T)>
using SmallVector =
    detail::VectorExtension<boost::container::small_vector<T, N>>;

} // namespace ren
