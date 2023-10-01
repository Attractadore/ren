#pragma once
#include "ren.hpp"

#include <imgui.h>

namespace ren::imgui {

void set_context(SceneId scene, ImGuiContext *ctx);

void enable(SceneId scene, bool value);

} // namespace ren::imgui
