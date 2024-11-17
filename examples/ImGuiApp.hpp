#pragma once
#include "AppBase.hpp"

#include <imgui.h>

class ImGuiApp : public AppBase {
public:
  ImGuiApp(const char *name);

protected:
  [[nodiscard]] auto process_event(const SDL_Event &e) -> Result<void> override;

  [[nodiscard]] auto begin_frame() -> Result<void> override;

  [[nodiscard]] auto end_frame() -> Result<void> override;

  auto imgui_wants_capture_keyboard() const -> bool;

  auto imgui_wants_capture_mouse() const -> bool;

private:
  struct Deleter {
    void operator()(ImGuiContext *context) const noexcept;
  };

  std::unique_ptr<ImGuiContext, Deleter> m_imgui_context;
  bool m_imgui_enabled = true;
  ImFont *m_font = nullptr;
  float m_ui_scale = 1.0f;
  float m_mouse_scale = 1.0f;
};
