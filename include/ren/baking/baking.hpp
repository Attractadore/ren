#pragma once
#include "../ren.hpp"

namespace ren {

struct Baker;

auto create_baker(Renderer *renderer) -> expected<Baker *>;

void destroy_baker(Baker *baker);

} // namespace ren
