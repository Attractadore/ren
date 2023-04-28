#include "Texture.hpp"
#include "Support/Math.hpp"

namespace ren {

auto get_mip_level_count(unsigned width, unsigned height, unsigned depth)
    -> unsigned {
  auto size = std::max({width, height, depth});
  return ilog2(size) + 1;
}

} // namespace ren
