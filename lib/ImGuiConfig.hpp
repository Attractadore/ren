#pragma once
#include "Config.hpp"
#if REN_IMGUI
#include "Support/Macros.hpp"

#include <imgui.h>

namespace ren {

class ImGuiScope {
public:
  ImGuiScope(ImGuiContext *context) {
    m_context = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(context);
  }

  ~ImGuiScope() { ImGui::SetCurrentContext(m_context); }

private:
  ImGuiContext *m_context = nullptr;
};

#define ren_imgui_scope_unique_name ren_cat(imgui_scope_, __LINE__)

#define ren_ImGuiScope(context) ImGuiScope ren_imgui_scope_unique_name(context)

} // namespace ren
#else
#define ren_ImGuiScope(context) (void)(context)
#endif
