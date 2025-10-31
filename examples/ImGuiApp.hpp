#pragma once
#include "AppBase.hpp"

#include <imgui.hpp>

class ImGuiApp : public AppBase {
public:
  void init(ren::String8 name);
  void quit();

protected:
  void process_event(const SDL_Event &e) override;

  void begin_frame() override;

  void end_frame() override;

  auto imgui_wants_capture_keyboard() const -> bool;

  auto imgui_wants_capture_mouse() const -> bool;

private:
  bool m_imgui_enabled = true;
  ImFont *m_font = nullptr;
};
