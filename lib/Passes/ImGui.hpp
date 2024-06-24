#pragma once
#if REN_IMGUI
#include "RenderGraph.hpp"
#include "Support/NotNull.hpp"
#include "Support/StdDef.hpp"

#include <glm/glm.hpp>

struct ImGuiContext;

namespace ren {

class Scene;

struct ImGuiPassConfig {
  NotNull<RgTextureId *> sdr;
  u32 num_vertices = 0;
  u32 num_indices = 0;
};

void setup_imgui_pass(RgBuilder &rgb, NotNull<const Scene *> scene,
                      const ImGuiPassConfig &cfg);

} // namespace ren

#endif // REN_IMGUI
