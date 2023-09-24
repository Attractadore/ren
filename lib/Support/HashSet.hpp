#pragma once
#include "Hash.hpp"

#include <unordered_set>

namespace ren {

template <typename T, typename KeyHash = Hash<T>,
          typename KeyEqual = std::equal_to<T>>
using HashSet = std::unordered_set<T, KeyHash, KeyEqual>;

} // namespace ren
