#pragma once
#include "Support/Variant.hpp"
#include "ren/ren.hpp"

namespace ren {

struct ToneMappingOptions {
  Variant<ReinhardToneMapping> oper;
};

} // namespace ren
