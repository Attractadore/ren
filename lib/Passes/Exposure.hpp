#pragma once
#include "RenderGraph.hpp"
#include "Support/NotNull.hpp"
#include "Support/StdDef.hpp"

namespace ren {

class Scene;

struct ExposurePassConfig {
  NotNull<RgTextureId *> exposure;
  NotNull<u32 *> temporal_layer;
};

void setup_exposure_pass(RgBuilder &rgb, NotNull<const Scene *> scene,
                         const ExposurePassConfig &cfg);

} // namespace ren
