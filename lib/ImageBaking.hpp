#pragma once
#include "core/Result.hpp"
#include "ren/baking/baking.hpp"
#include "ren/baking/image.hpp"

namespace ren {

auto bake_dhr_lut_to_memory(IBaker *baker) -> Result<Blob, Error>;

auto bake_ibl_to_memory(IBaker *baker, const TextureInfo &info)
    -> Result<Blob, Error>;

} // namespace ren
