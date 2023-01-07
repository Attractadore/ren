#pragma once
#include "Descriptors.hpp"
#include "Pipeline.hpp"

namespace ren {

struct Material {
  PipelineRef pipeline;
  unsigned index;
};

} // namespace ren
