#pragma once
#include "HashMap.hpp"
#include "SlotMapKey.hpp"

namespace ren {

template <typename T, CSlotMapKey K = SlotMapKey>
using SecondaryMap = HashMap<K, T>;

} // namespace ren
