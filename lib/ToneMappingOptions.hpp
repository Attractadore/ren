#pragma once
#include "Support/Variant.hpp"

namespace ren {

struct ReinhardToneMapping {};

struct ToneMappingOptions {
  Variant<ReinhardToneMapping> oper;
};

} // namespace ren
