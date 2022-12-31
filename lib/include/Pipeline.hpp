#pragma once
#include "Support/Ref.hpp"

namespace ren {

struct PipelineDesc {};

struct Pipeline {
  PipelineDesc desc;
  AnyRef handle;
};

struct PipelineRef {
  PipelineDesc desc;
  void *handle;
};

} // namespace ren
