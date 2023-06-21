#pragma once
#include "RenderGraph.hpp"
#include "Support/Variant.hpp"

namespace ren {

struct ExposureOptions {
  struct Manual {
    float exposure = 0.00005f;
  };

  struct Camera {
    float aperture = 16.0f;
    float shutter_time = 1.0f / 100.0f;
    float iso = 100.0f;
    float exposure_compensation = 0.0f;
  };

  struct Automatic {
    RGBufferID previous_exposure_buffer;
    float exposure_compensation = 0.0f;
  };

  using Mode = Variant<Manual, Camera, Automatic>;

  Mode mode = Automatic();
};

} // namespace ren
