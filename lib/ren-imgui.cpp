#if REN_IMGUI
#include "ren/ren-imgui.hpp"
#include "Lippincott.hpp"
#include "Scene.hpp"

namespace ren::imgui {

void set_context(IScene &scene, ImGuiContext *ctx) {
  static_cast<Scene &>(scene).set_imgui_context(ctx);
}

auto get_context(IScene &scene) -> ImGuiContext * {
  return static_cast<Scene &>(scene).get_imgui_context();
}

auto draw(IScene &scene) -> expected<void> {
  return lippincott([&] { return static_cast<Scene &>(scene).draw_imgui(); });
}

} // namespace ren::imgui

#endif // REN_IMGUI
