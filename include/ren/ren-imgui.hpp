#pragma once
#include "ren.hpp"

#include <imgui.h>

namespace ren::imgui {

void set_context(Scene *scene, ImGuiContext *ctx);

auto get_context(Scene *scene) -> ImGuiContext *;

auto draw(Scene *scene) -> expected<void>;

} // namespace ren::imgui
