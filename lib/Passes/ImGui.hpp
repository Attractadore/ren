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
  RgTextureId rt;
  u32 num_vertices = 0;
  u32 num_indices = 0;
};

auto setup_imgui_pass(RgBuilder &rgb, NotNull<const Scene *> scene,
                      const ImGuiPassConfig &cfg) -> RgTextureId;

} // namespace ren

#endif // REN_IMGUI
