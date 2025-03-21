#pragma once
#include "ren/baking/baking.hpp"

namespace ren {

auto bake_dhr_lut_to_memory(IBaker *baker)
    -> expected<std::tuple<void *, size_t>>;

}
