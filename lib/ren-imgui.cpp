#include "Config.hpp"
#if REN_IMGUI
#include "Scene.hpp"
#include "ren/ren-imgui.hpp"

namespace ren::imgui {

void set_context(SceneId scene, ImGuiContext *ctx) {
  get_scene(scene)->set_imgui_context(ctx);
}

auto get_context(SceneId scene) -> ImGuiContext * {
  return get_scene(scene)->get_imgui_context();
}

} // namespace ren::imgui

#endif // REN_IMGUI
