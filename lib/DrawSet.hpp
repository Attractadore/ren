#pragma once
#include "Support/Flags.hpp"
#include "Support/NewType.hpp"
#include "Support/StdDef.hpp"

#include <bit>

namespace ren {

REN_BEGIN_FLAGS_ENUM(DrawSet){
    REN_FLAG(DepthOnly),
    REN_FLAG(Opaque),
} REN_END_FLAGS_ENUM(DrawSet);

constexpr inline auto get_draw_set_name(DrawSet set) -> const char * {
  switch (set) {
  case DrawSet::DepthOnly:
    return "depth-only";
  case DrawSet::Opaque:
    return "opaque";
  }
}

constexpr inline auto get_draw_set_index(DrawSet set) -> usize {
  return std::countr_zero((usize)set);
}

constexpr usize NUM_DRAW_SETS = get_draw_set_index(DrawSet::Opaque) + 1;

REN_NEW_TYPE(DrawSetId, u32);
constexpr DrawSetId InvalidDrawSetId(-1);

} // namespace ren
