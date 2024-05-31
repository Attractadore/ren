#pragma once
#include "ren.hpp"

#include <imgui.h>

namespace ren::imgui {

void set_context(IScene &scene, ImGuiContext *ctx);

auto get_context(IScene &scene) -> ImGuiContext *;

auto draw(IScene &scene) -> expected<void>;

} // namespace ren::imgui
