#pragma once
#include "Pipeline.hpp"

namespace ren {

struct Material {
  PipelineRef pipeline;
  unsigned index;
};

} // namespace ren
