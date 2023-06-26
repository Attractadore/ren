#pragma once
#include <boost/container/small_vector.hpp>
#include <boost/container/static_vector.hpp>
#include <boost/predef/compiler.h>
#include <range/v3/range.hpp>

#include <vector>

namespace ren {
namespace detail {
template <class Base> struct VectorMixin : public Base {
  using Base::Base;

  template <ranges::input_range R> VectorMixin(R &&range) {
    this->assign(std::forward<R>(range));
  }

  constexpr auto append(ranges::input_range auto &&r) {
    return this->append(ranges::begin(r), ranges::end(r));
  }

  template <std::input_iterator I, std::sentinel_for<I> S>
  constexpr auto append(I i, S s) {
    if constexpr (std::same_as<I, S>) {
      return this->insert(this->end(), i, s);
    } else {
      using CI = std::common_iterator<I, S>;
      return this->insert(this->end(), CI(i), CI(s));
    }
  }

  using Base::assign;

  constexpr void assign(ranges::input_range auto &&r) {
    this->assign(ranges::begin(r), ranges::end(r));
  }

  template <std::input_iterator I, std::sentinel_for<I> S>
    requires(not std::same_as<I, S>)
  constexpr void assign(I i, S s) {
    using CI = std::common_iterator<I, S>;
    this->assign(CI(i), CI(s));
  }
};
} // namespace detail

// FIXME: keep these hacks until Clang implements CTAD for aliases
#if BOOST_COMP_CLANG
template <typename T> struct Vector : detail::VectorMixin<std::vector<T>> {
  using mixin = detail::VectorMixin<std::vector<T>>;
  using mixin::mixin;
};

template <typename T, size_t N = 64 / sizeof(T)>
struct SmallVector : detail::VectorMixin<boost::container::small_vector<T, N>> {
  using mixin = detail::VectorMixin<boost::container::small_vector<T, N>>;
  using mixin::mixin;
};

template <typename T> struct TinyVector : SmallVector<T> {
  using SmallVector<T>::SmallVector;
};

template <typename T, size_t N>
struct StaticVector
    : detail::VectorMixin<boost::container::static_vector<T, N>> {
  using mixin = detail::VectorMixin<boost::container::static_vector<T, N>>;
  using mixin::mixin;
};
#else
template <typename T> using Vector = detail::VectorMixin<std::vector<T>>;

template <typename T, size_t N = 64 / sizeof(T)>
using SmallVector = detail::VectorMixin<boost::container::small_vector<T, N>>;

// Typedef for ranges::to
template <typename T> using TinyVector = SmallVector<T>;

template <typename T, size_t N>
using StaticVector = detail::VectorMixin<boost::container::static_vector<T, N>>;
#endif

namespace detail {
template <size_t N> struct SizedSmallVector {
  template <typename T> using type = SmallVector<T, N>;
};
} // namespace detail

} // namespace ren
