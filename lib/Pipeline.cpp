#include "Pipeline.hpp"
#include "ResourceArena.hpp"
#include "ren/core/Format.hpp"

namespace ren {

#undef load_compute_pipeline
auto load_compute_pipeline(ResourceArena &arena, Span<const std::byte> shader,
                           String8 name)
    -> Result<Handle<ComputePipeline>, Error> {
  ScratchArena scratch;
  return arena.create_compute_pipeline({
      .name = format(scratch, "{} compute pipeline", name),
      .cs = {shader},
  });
};

} // namespace ren
