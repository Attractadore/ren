#pragma once
#include "../ren.hpp"

namespace ren {

struct IBaker;

auto create_baker(IRenderer *renderer) -> expected<IBaker *>;

void destroy_baker(IBaker *baker);

} // namespace ren
