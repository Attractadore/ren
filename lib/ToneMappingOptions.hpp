#pragma once
#include "ren/ren.h"

namespace ren {

struct ToneMappingOptions {
  RenToneMappingOperator oper = REN_TONE_MAPPING_OPERATOR_REINHARD;
};

} // namespace ren
