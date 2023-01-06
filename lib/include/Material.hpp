#pragma once
#include "Descriptors.hpp"
#include "Pipeline.hpp"

namespace ren {

struct Material {
  PipelineRef pipeline;
  unsigned index;
};

struct MaterialLayout {
  DescriptorSetLayout persistent_set;
  DescriptorSetLayout global_set;
  PipelineSignature pipeline_signature;
};

} // namespace ren
