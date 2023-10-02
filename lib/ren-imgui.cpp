#include "Config.hpp"
#if REN_IMGUI
#include "Scene.hpp"
#include "ren/ren-imgui.hpp"

namespace ren::imgui {

void set_context(SceneId scene, ImGuiContext *ctx) {
  get_scene(scene)->set_imgui_context(ctx);
}

} // namespace ren::imgui

#endif // REN_IMGUI
