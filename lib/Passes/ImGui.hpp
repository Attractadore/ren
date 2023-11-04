#pragma once
#include "Config.hpp"
#if REN_IMGUI
#include "Handle.hpp"

#include <glm/glm.hpp>

struct ImGuiContext;

namespace ren {

struct GraphicsPipeline;
class RgBuilder;

struct ImGuiPassConfig {
  ImGuiContext *imgui_context = nullptr;
  Handle<GraphicsPipeline> pipeline;
  glm::uvec2 viewport;
};

void setup_imgui_pass(RgBuilder &rgb, const ImGuiPassConfig &cfg);

} // namespace ren

#endif // REN_IMGUI
