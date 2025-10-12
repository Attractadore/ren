#pragma once
#include "../ren.hpp"

namespace ren {

struct Baker;

auto create_baker(NotNull<Arena *> arena, Renderer *renderer)
    -> expected<Baker *>;

void destroy_baker(Baker *baker);

} // namespace ren
