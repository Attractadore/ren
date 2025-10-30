#pragma once
#include "../ren.hpp"

namespace ren {

struct Baker;

Baker *create_baker(NotNull<Arena *> arena, Renderer *renderer);

void destroy_baker(Baker *baker);

} // namespace ren
