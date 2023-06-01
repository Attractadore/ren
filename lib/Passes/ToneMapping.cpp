#include "Passes/ToneMapping.hpp"
#include "Errors.hpp"
#include "Passes/ReinhardToneMapping.hpp"

namespace ren {

auto setup_tone_mapping_pass(Device &device, RenderGraph::Builder &rgb,
                             const ToneMappingPassConfig &cfg)
    -> ToneMappingPassOutput {
  switch (cfg.options.oper) {
  case REN_TONE_MAPPING_OPERATOR_REINHARD: {
    return setup_reinhard_tone_mapping_pass(
        device, rgb,
        {
            .texture = cfg.texture,
            .texture_allocator = cfg.texture_allocator,
            .pipelines = cfg.pipelines,
        });
  }
  case REN_TONE_MAPPING_OPERATOR_ACES: {
    todo("ACES tone mapping is not implemented!");
  }
  }
  unreachable("Unknown tone mapping operator");
}

} // namespace ren
