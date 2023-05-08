#pragma once
#include "Handle.hpp"
#include "Support/SlotMap.hpp"

namespace ren {

template <typename T> using HandleMap = SlotMap<T, Handle<T>>;

}
