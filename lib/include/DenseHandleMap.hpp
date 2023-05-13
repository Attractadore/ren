#pragma once
#include "Handle.hpp"
#include "Support/DenseSlotMap.hpp"

namespace ren {

template <typename T> using DenseHandleMap = DenseSlotMap<T, Handle<T>>;

}
