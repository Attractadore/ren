#pragma once
#include <boost/container/flat_map.hpp>

namespace ren {

template <typename K, typename V>
using FlatMap = boost::container::flat_map<K, V>;

template <typename K, typename V, size_t N>
using SmallFlatMap = boost::container::small_flat_map<K, V, N>;

} // namespace ren
