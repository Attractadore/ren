#include "Config.hpp"
#if REN_IMGUI
#include "Scene.hpp"
#include "ren/ren-imgui.hpp"

namespace ren::imgui {

void set_context(SceneId scene, ImGuiContext *ctx) {
  get_scene(scene)->set_imgui_context(ctx);
}

void enable(SceneId scene, bool value) {
  get_scene(scene)->enable_imgui(value);
}

} // namespace ren::imgui

#endif // REN_IMGUI
