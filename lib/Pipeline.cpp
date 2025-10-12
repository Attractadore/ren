#include "Pipeline.hpp"
#include "ResourceArena.hpp"

#include <fmt/format.h>

namespace ren {

#undef load_compute_pipeline
auto load_compute_pipeline(Arena scratch, ResourceArena &arena,
                           Span<const std::byte> shader, StringView name)
    -> Result<Handle<ComputePipeline>, Error> {
  return arena.create_compute_pipeline(
      scratch, {
                   .name = fmt::format("{} compute pipeline", name),
                   .cs = {shader},
               });
};

} // namespace ren
