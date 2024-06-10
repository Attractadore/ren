#pragma once
#include "RenderGraph.hpp"
#include "Support/NotNull.hpp"
#include "Support/StdDef.hpp"

namespace ren {

class Scene;

struct ExposurePassOutput {
  RgTextureId exposure;
  u32 temporal_layer = 0;
};

auto setup_exposure_pass(RgBuilder &rgb, NotNull<const Scene *> scene)
    -> ExposurePassOutput;

} // namespace ren
