#pragma once
#include <unordered_map>

#include "Hash.hpp"

namespace ren {

template <typename K, typename V>
using HashMap = std::unordered_map<K, V, Hash<K>>;
}
