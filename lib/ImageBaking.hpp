#pragma once
#include "core/Result.hpp"
#include "ren/baking/baking.hpp"
#include "ren/baking/image.hpp"

namespace ren {

auto bake_ibl_to_memory(Baker *baker, const TextureInfo &info,
                        bool compress = true) -> Result<Blob, Error>;

} // namespace ren
