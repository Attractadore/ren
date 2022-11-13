#pragma once
#include "Attractadore/TrivialVector.hpp"

#include <boost/container/small_vector.hpp>
#include <boost/container/static_vector.hpp>
#include <range/v3/range/concepts.hpp>
#include <range/v3/view/common.hpp>

#include <vector>

namespace ren {
using Attractadore::InlineTrivialVector;
using Attractadore::TrivialVector;

namespace detail {
template <typename T, typename Allocator> struct VectorImpl {
  using type = std::vector<T, Allocator>;
};

template <typename T> struct VectorImpl<T, void> {
  using type = std::vector<T>;
};

template <typename T, typename Allocator>
requires std::is_trivial_v<T>
struct VectorImpl<T, Allocator> {
  using type = TrivialVector<T, Allocator>;
};

template <typename T>
requires std::is_trivial_v<T>
struct VectorImpl<T, void> {
  using type = TrivialVector<T>;
};

template <typename T, unsigned BufferSize>
constexpr auto BufferCount = BufferSize / sizeof(T);

template <typename T, unsigned InlineCapacity, typename Allocator>
struct SmallVectorImpl {
  using type = boost::container::small_vector<T, InlineCapacity, Allocator>;
};

template <typename T, unsigned InlineCapacity>
struct SmallVectorImpl<T, InlineCapacity, void> {
  using type = boost::container::small_vector<T, InlineCapacity>;
};

template <typename T, unsigned InlineCapacity, typename Allocator>
requires std::is_trivial_v<T>
struct SmallVectorImpl<T, InlineCapacity, Allocator> {
  using type = InlineTrivialVector<T, InlineCapacity, Allocator>;
};

template <typename T, unsigned InlineCapacity>
requires std::is_trivial_v<T>
struct SmallVectorImpl<T, InlineCapacity, void> {
  using type = InlineTrivialVector<T, InlineCapacity>;
};

}; // namespace detail

template <typename T, typename Allocator = void>
using Vector = typename detail::VectorImpl<T, Allocator>::type;

template <typename T, size_t InlineCapacity = detail::BufferCount<T, 64>,
          typename Allocator = void>
using SmallVector =
    typename detail::SmallVectorImpl<T, InlineCapacity, Allocator>::type;

template <typename T, unsigned InlineBufferCapacity, typename Allocator = void>
using SmallVectorBytes =
    SmallVector<T, detail::BufferCount<T, InlineBufferCapacity>, Allocator>;

template <typename T, typename Allocator = void>
using SmallVector64B = SmallVectorBytes<T, 64, Allocator>;

template <typename T, typename Allocator = void>
using SmallVector128B = SmallVectorBytes<T, 128, Allocator>;

template <typename T, typename Allocator = void>
using SmallVector256B = SmallVectorBytes<T, 256, Allocator>;

template <typename T, typename Allocator = void>
using SmallVector512B = SmallVectorBytes<T, 512, Allocator>;

template <typename T, typename Allocator = void>
using SmallVector1K = SmallVectorBytes<T, 1024, Allocator>;

template <typename T, typename Allocator = void>
using SmallVector2K = SmallVectorBytes<T, 2048, Allocator>;

template <typename T, typename Allocator = void>
using SmallVector4K = SmallVectorBytes<T, 4096, Allocator>;

template <size_t N> struct SizedSmallVector {
  template <typename T> using impl = SmallVector<T, N>;
};

template <typename T, unsigned InlineCapacity>
using StaticVector = boost::container::static_vector<T, InlineCapacity>;

template <typename Vec, ranges::input_range R>
constexpr auto VecAppend(Vec &vec, R &&r) {
  auto v = ranges::views::common(r);
  return vec.insert(vec.end(), v.begin(), v.end());
}

template <typename Vec, std::input_iterator I, std::sentinel_for<I> S>
constexpr auto VecAppend(Vec &vec, I i, S s) {
  if constexpr (std::same_as<I, S>) {
    return vec.insert(vec.end(), std::move(i), std::move(s));
  } else {
    using CI = std::common_iterator<I, S>;
    return vec.insert(vec.end(), CI(std::move(i)), CI(std::move(s)));
  }
}
} // namespace ren
