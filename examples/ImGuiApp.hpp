#pragma once
#include "AppBase.hpp"

#include <imgui.hpp>

class ImGuiApp : public AppBase {
public:
  auto init(const char *name) -> Result<void>;
  ~ImGuiApp();

protected:
  [[nodiscard]] auto process_event(const SDL_Event &e) -> Result<void> override;

  [[nodiscard]] auto begin_frame() -> Result<void> override;

  [[nodiscard]] auto end_frame() -> Result<void> override;

  auto imgui_wants_capture_keyboard() const -> bool;

  auto imgui_wants_capture_mouse() const -> bool;

private:
  bool m_imgui_enabled = true;
  ImFont *m_font = nullptr;
};
