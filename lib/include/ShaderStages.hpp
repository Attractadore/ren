#pragma once
#include "Support/Enum.hpp"

namespace ren {

#define REN_SHADER_STAGES (Vertex)(Fragment)(Compute)
REN_DEFINE_FLAGS_ENUM(ShaderStage, REN_SHADER_STAGES);

} // namespace ren
