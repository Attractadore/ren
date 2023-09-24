#pragma once
#include "DenseSlotMap.hpp"

namespace ren {

template <typename T, CSlotMapKey K = SlotMapKey,
          template <typename> typename C = Vector>
using SlotMap = DenseSlotMap<T, K, C>;

}
