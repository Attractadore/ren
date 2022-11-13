#pragma once
#include <unordered_map>

namespace ren {
template <typename K, typename V> using HashMap = std::unordered_map<K, V>;
}
