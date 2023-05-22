#pragma once
#include "ren/ren.h"

namespace ren {

struct PostprocessingOptions {
  struct Camera {
    float aperture = 16.0f;
    float shutter_time = 1.0f / 400.0f;
    float iso = 400.0f;
  } camera;

  struct Exposure {
    struct Automatic {};

    struct Camera {};

    struct Manual {
      float exposure;
    };

    using Mode = std::variant<Automatic, Camera, Manual>;

    float compensation = 0.0f;
    Mode mode = Camera{};
  } exposure;

  struct Tonemapping {
    RenTonemappingOperator oper = REN_TONEMAPPING_OPERATOR_REINHARD;
  } tonemapping;
};

} // namespace ren
