#pragma once
#if REN_IMGUI
#include "Pass.hpp"

class ImGuiContext;

namespace ren {

struct ImGuiPassConfig {
  ImGuiContext *ctx = nullptr;
  NotNull<RgTextureId *> sdr;
};

void setup_imgui_pass(const PassCommonConfig &ccfg, const ImGuiPassConfig &cfg);

} // namespace ren

#endif // REN_IMGUI
