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

template <> struct Hash<BatchDesc> {
  auto operator()(const BatchDesc &value) const noexcept -> u64 {
    u64 seed = 0;
    seed = hash_combine(seed, value.pipeline);
    seed = hash_combine(seed, value.index_buffer);
    return seed;
  }
};

} // namespace ren
