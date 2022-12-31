#pragma once
#include "Support/Ref.hpp"

namespace ren {

struct PipelineDesc {};

struct PipelineRef {
  PipelineDesc desc;
  void *handle;
};

struct Pipeline {
  PipelineDesc desc;
  AnyRef handle;

  operator PipelineRef() const {
    return {.desc = desc, .handle = handle.get()};
  }
};

} // namespace ren
