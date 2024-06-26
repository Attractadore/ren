#pragma once
#if REN_IMGUI
#include "Pass.hpp"

namespace ren {

struct ImGuiPassConfig {
  NotNull<RgTextureId *> sdr;
};

void setup_imgui_pass(const PassCommonConfig &ccfg, const ImGuiPassConfig &cfg);

} // namespace ren

#endif // REN_IMGUI
