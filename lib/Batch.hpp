#pragma once
#include "Buffer.hpp"
#include "Handle.hpp"
#include "Support/Hash.hpp"

namespace ren {

struct GraphicsPipeline;

struct BatchDesc {
  Handle<GraphicsPipeline> pipeline;
  BufferView index_buffer_view;

  bool operator==(const BatchDesc &) const = default;
};

REN_DEFINE_TYPE_HASH(BatchDesc, pipeline, index_buffer_view);

} // namespace ren
