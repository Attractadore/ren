#pragma once
#include "Config.hpp"
#if REN_IMGUI
#include "Handle.hpp"

#include <glm/glm.hpp>

struct ImGuiContext;

namespace ren {

struct GraphicsPipeline;
class RgBuilder;

struct ImGuiPassData {
  const ImGuiContext *context = nullptr;
};

struct ImGuiPassConfig {
  Handle<GraphicsPipeline> pipeline;
  glm::uvec2 fb_size;
};

void setup_imgui_pass(RgBuilder &rgb, const ImGuiPassConfig &cfg);

} // namespace ren

#endif // REN_IMGUI
