#pragma once
#include <boost/container/flat_set.hpp>

namespace ren {
template <typename K> using FlatSet = boost::container::flat_set<K>;

template <typename K, size_t N = 64 / sizeof(K)>
using SmallFlatSet = boost::container::small_flat_set<K, N>;
} // namespace ren
