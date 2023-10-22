#include "Config.hpp"
#if REN_IMGUI
#include "Lippincott.hpp"
#include "Scene.hpp"
#include "ren/ren-imgui.hpp"

namespace ren::imgui {

void set_context(SceneId scene, ImGuiContext *ctx) {
  get_scene(scene)->set_imgui_context(ctx);
}

auto get_context(SceneId scene) -> ImGuiContext * {
  return get_scene(scene)->get_imgui_context();
}

auto draw(SceneId scene) -> expected<void> {
  return lippincott([&] { get_scene(scene)->draw_imgui(); });
}

} // namespace ren::imgui

#endif // REN_IMGUI
