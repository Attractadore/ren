#pragma once
#include "core/Flags.hpp"
#include "core/NewType.hpp"
#include "core/StdDef.hpp"

#include <bit>
#include <utility>

namespace ren {

REN_BEGIN_FLAGS_ENUM(DrawSet){
    REN_FLAG(DepthOnly),
    REN_FLAG(Opaque),
    Last = Opaque,
} REN_END_FLAGS_ENUM(DrawSet);

}

REN_ENABLE_FLAGS(ren::DrawSet);

namespace ren {

using DrawSetFlags = Flags<DrawSet>;

constexpr inline auto get_draw_set_name(DrawSet set) -> const char * {
  switch (set) {
  case DrawSet::DepthOnly:
    return "depth-only";
  case DrawSet::Opaque:
    return "opaque";
  }
  std::unreachable();
}

constexpr inline auto get_draw_set_index(DrawSet set) -> u32 {
  return std::countr_zero((u32)set);
}

constexpr usize NUM_DRAW_SETS = get_draw_set_index(DrawSet::Last) + 1;

REN_NEW_TYPE(DrawSetId, u32);
constexpr DrawSetId InvalidDrawSetId(-1);

} // namespace ren
