#pragma once
#include "Buffer.hpp"
#include "core/GenIndex.hpp"
#include "core/Hash.hpp"

namespace ren {

struct GraphicsPipeline;

struct BatchDesc {
  Handle<GraphicsPipeline> pipeline;
  Handle<Buffer> index_buffer;

public:
  bool operator==(const BatchDesc &) const = default;
};

REN_DEFINE_TYPE_HASH(BatchDesc, pipeline, index_buffer);

} // namespace ren
