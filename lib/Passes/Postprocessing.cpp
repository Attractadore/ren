#include "Passes/Postprocessing.hpp"
#include "Passes/ToneMapping.hpp"

namespace ren {

auto setup_postprocessing_passes(Device &device, RenderGraph::Builder &rgb,
                                 const PostprocessingPassesConfig &cfg)
    -> PostprocessingPassesOutput {
  auto texture = cfg.texture;

  auto tone_mapping =
      setup_tone_mapping_pass(device, rgb,
                              {
                                  .texture = texture,
                                  .options = cfg.options.tone_mapping,
                                  .texture_allocator = cfg.texture_allocator,
                                  .pipelines = cfg.pipelines,
                              });
  texture = tone_mapping.texture;

  return {
      .texture = texture,
  };
}

} // namespace ren
